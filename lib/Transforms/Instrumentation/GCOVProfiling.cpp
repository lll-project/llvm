//===- GCOVProfiling.cpp - Insert edge counters for gcov profiling --------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements GCOV-style profiling. When this pass is run it emits
// "gcno" files next to the existing source, and instruments the code that runs
// to records the edges between blocks that run and emit a complementary "gcda"
// file on exit.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "insert-gcov-profiling"

#include "ProfilingUtils.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugLoc.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/PathV2.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/UniqueVector.h"
#include <string>
#include <utility>
using namespace llvm;

namespace {
  class GCOVProfiler : public ModulePass {
  public:
    static char ID;
    GCOVProfiler()
        : ModulePass(ID), EmitNotes(true), EmitData(true), Use402Format(false) {
      initializeGCOVProfilerPass(*PassRegistry::getPassRegistry());
    }
    GCOVProfiler(bool EmitNotes, bool EmitData, bool use402Format = false)
        : ModulePass(ID), EmitNotes(EmitNotes), EmitData(EmitData),
          Use402Format(use402Format) {
      assert((EmitNotes || EmitData) && "GCOVProfiler asked to do nothing?");
      initializeGCOVProfilerPass(*PassRegistry::getPassRegistry());
    }
    virtual const char *getPassName() const {
      return "GCOV Profiler";
    }

  private:
    bool runOnModule(Module &M);

    // Create the GCNO files for the Module based on DebugInfo.
    void emitGCNO(DebugInfoFinder &DIF);

    // Modify the program to track transitions along edges and call into the
    // profiling runtime to emit .gcda files when run.
    bool emitProfileArcs(DebugInfoFinder &DIF);

    // Get pointers to the functions in the runtime library.
    Constant *getStartFileFunc();
    Constant *getIncrementIndirectCounterFunc();
    Constant *getEmitFunctionFunc();
    Constant *getEmitArcsFunc();
    Constant *getEndFileFunc();

    // Create or retrieve an i32 state value that is used to represent the
    // pred block number for certain non-trivial edges.
    GlobalVariable *getEdgeStateValue();

    // Produce a table of pointers to counters, by predecessor and successor
    // block number.
    GlobalVariable *buildEdgeLookupTable(Function *F,
                                         GlobalVariable *Counter,
                                         const UniqueVector<BasicBlock *> &Preds,
                                         const UniqueVector<BasicBlock *> &Succs);

    // Add the function to write out all our counters to the global destructor
    // list.
    void insertCounterWriteout(DebugInfoFinder &,
                               SmallVector<std::pair<GlobalVariable *,
                                                     MDNode *>, 8> &);

    std::string mangleName(DICompileUnit CU, std::string NewStem);

    bool EmitNotes;
    bool EmitData;
    bool Use402Format;

    Module *M;
    LLVMContext *Ctx;
  };
}

char GCOVProfiler::ID = 0;
INITIALIZE_PASS(GCOVProfiler, "insert-gcov-profiling",
                "Insert instrumentation for GCOV profiling", false, false)

ModulePass *llvm::createGCOVProfilerPass(bool EmitNotes, bool EmitData,
                                         bool Use402Format) {
  return new GCOVProfiler(EmitNotes, EmitData, Use402Format);
}

static DISubprogram findSubprogram(DIScope Scope) {
  while (!Scope.isSubprogram()) {
    assert(Scope.isLexicalBlock() &&
           "Debug location not lexical block or subprogram");
    Scope = DILexicalBlock(Scope).getContext();
  }
  return DISubprogram(Scope);
}

namespace {
  class GCOVRecord {
   protected:
    static const char *LinesTag;
    static const char *FunctionTag;
    static const char *BlockTag;
    static const char *EdgeTag;

    GCOVRecord() {}

    void writeBytes(const char *Bytes, int Size) {
      os->write(Bytes, Size);
    }

    void write(uint32_t i) {
      writeBytes(reinterpret_cast<char*>(&i), 4);
    }

    // Returns the length measured in 4-byte blocks that will be used to
    // represent this string in a GCOV file
    unsigned lengthOfGCOVString(StringRef s) {
      // A GCOV string is a length, followed by a NUL, then between 0 and 3 NULs
      // padding out to the next 4-byte word. The length is measured in 4-byte
      // words including padding, not bytes of actual string.
      return (s.size() / 4) + 1;
    }

    void writeGCOVString(StringRef s) {
      uint32_t Len = lengthOfGCOVString(s);
      write(Len);
      writeBytes(s.data(), s.size());

      // Write 1 to 4 bytes of NUL padding.
      assert((unsigned)(4 - (s.size() % 4)) > 0);
      assert((unsigned)(4 - (s.size() % 4)) <= 4);
      writeBytes("\0\0\0\0", 4 - (s.size() % 4));
    }

    raw_ostream *os;
  };
  const char *GCOVRecord::LinesTag = "\0\0\x45\x01";
  const char *GCOVRecord::FunctionTag = "\0\0\0\1";
  const char *GCOVRecord::BlockTag = "\0\0\x41\x01";
  const char *GCOVRecord::EdgeTag = "\0\0\x43\x01";

  class GCOVFunction;
  class GCOVBlock;

  // Constructed only by requesting it from a GCOVBlock, this object stores a
  // list of line numbers and a single filename, representing lines that belong
  // to the block.
  class GCOVLines : public GCOVRecord {
   public:
    void addLine(uint32_t Line) {
      Lines.push_back(Line);
    }

    uint32_t length() {
      return lengthOfGCOVString(Filename) + 2 + Lines.size();
    }

   private:
    friend class GCOVBlock;

    GCOVLines(std::string Filename, raw_ostream *os)
        : Filename(Filename) {
      this->os = os;
    }

    std::string Filename;
    SmallVector<uint32_t, 32> Lines;
  };

  // Represent a basic block in GCOV. Each block has a unique number in the
  // function, number of lines belonging to each block, and a set of edges to
  // other blocks.
  class GCOVBlock : public GCOVRecord {
   public:
    GCOVLines &getFile(std::string Filename) {
      GCOVLines *&Lines = LinesByFile[Filename];
      if (!Lines) {
        Lines = new GCOVLines(Filename, os);
      }
      return *Lines;
    }

    void addEdge(GCOVBlock &Successor) {
      OutEdges.push_back(&Successor);
    }

    void writeOut() {
      uint32_t Len = 3;
      for (StringMap<GCOVLines *>::iterator I = LinesByFile.begin(),
               E = LinesByFile.end(); I != E; ++I) {
        Len += I->second->length();
      }

      writeBytes(LinesTag, 4);
      write(Len);
      write(Number);
      for (StringMap<GCOVLines *>::iterator I = LinesByFile.begin(),
               E = LinesByFile.end(); I != E; ++I) {
        write(0);
        writeGCOVString(I->second->Filename);
        for (int i = 0, e = I->second->Lines.size(); i != e; ++i) {
          write(I->second->Lines[i]);
        }
      }
      write(0);
      write(0);
    }

    ~GCOVBlock() {
      DeleteContainerSeconds(LinesByFile);
    }

   private:
    friend class GCOVFunction;

    GCOVBlock(uint32_t Number, raw_ostream *os)
        : Number(Number) {
      this->os = os;
    }

    uint32_t Number;
    StringMap<GCOVLines *> LinesByFile;
    SmallVector<GCOVBlock *, 4> OutEdges;
  };

  // A function has a unique identifier, a checksum (we leave as zero) and a
  // set of blocks and a map of edges between blocks. This is the only GCOV
  // object users can construct, the blocks and lines will be rooted here.
  class GCOVFunction : public GCOVRecord {
   public:
    GCOVFunction(DISubprogram SP, raw_ostream *os, bool Use402Format) {
      this->os = os;

      Function *F = SP.getFunction();
      uint32_t i = 0;
      for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
        Blocks[BB] = new GCOVBlock(i++, os);
      }
      ReturnBlock = new GCOVBlock(i++, os);

      writeBytes(FunctionTag, 4);
      uint32_t BlockLen = 1 + 1 + 1 + lengthOfGCOVString(SP.getName()) +
          1 + lengthOfGCOVString(SP.getFilename()) + 1;
      if (!Use402Format)
        ++BlockLen; // For second checksum.
      write(BlockLen);
      uint32_t Ident = reinterpret_cast<intptr_t>((MDNode*)SP);
      write(Ident);
      write(0);  // checksum #1
      if (!Use402Format)
        write(0);  // checksum #2
      writeGCOVString(SP.getName());
      writeGCOVString(SP.getFilename());
      write(SP.getLineNumber());
    }

    ~GCOVFunction() {
      DeleteContainerSeconds(Blocks);
      delete ReturnBlock;
    }

    GCOVBlock &getBlock(BasicBlock *BB) {
      return *Blocks[BB];
    }

    GCOVBlock &getReturnBlock() {
      return *ReturnBlock;
    }

    void writeOut() {
      // Emit count of blocks.
      writeBytes(BlockTag, 4);
      write(Blocks.size() + 1);
      for (int i = 0, e = Blocks.size() + 1; i != e; ++i) {
        write(0);  // No flags on our blocks.
      }

      // Emit edges between blocks.
      for (DenseMap<BasicBlock *, GCOVBlock *>::iterator I = Blocks.begin(),
               E = Blocks.end(); I != E; ++I) {
        GCOVBlock &Block = *I->second;
        if (Block.OutEdges.empty()) continue;

        writeBytes(EdgeTag, 4);
        write(Block.OutEdges.size() * 2 + 1);
        write(Block.Number);
        for (int i = 0, e = Block.OutEdges.size(); i != e; ++i) {
          write(Block.OutEdges[i]->Number);
          write(0);  // no flags
        }
      }

      // Emit lines for each block.
      for (DenseMap<BasicBlock *, GCOVBlock *>::iterator I = Blocks.begin(),
               E = Blocks.end(); I != E; ++I) {
        I->second->writeOut();
      }
    }

   private:
    DenseMap<BasicBlock *, GCOVBlock *> Blocks;
    GCOVBlock *ReturnBlock;
  };
}

std::string GCOVProfiler::mangleName(DICompileUnit CU, std::string NewStem) {
  if (NamedMDNode *GCov = M->getNamedMetadata("llvm.gcov")) {
    for (int i = 0, e = GCov->getNumOperands(); i != e; ++i) {
      MDNode *N = GCov->getOperand(i);
      if (N->getNumOperands() != 2) continue;
      MDString *GCovFile = dyn_cast<MDString>(N->getOperand(0));
      MDNode *CompileUnit = dyn_cast<MDNode>(N->getOperand(1));
      if (!GCovFile || !CompileUnit) continue;
      if (CompileUnit == CU) {
        SmallString<128> Filename = GCovFile->getString();
        sys::path::replace_extension(Filename, NewStem);
        return Filename.str();
      }
    }
  }

  SmallString<128> Filename = CU.getFilename();
  sys::path::replace_extension(Filename, NewStem);
  return sys::path::filename(Filename.str());
}

bool GCOVProfiler::runOnModule(Module &M) {
  this->M = &M;
  Ctx = &M.getContext();

  DebugInfoFinder DIF;
  DIF.processModule(M);

  if (EmitNotes) emitGCNO(DIF);
  if (EmitData) return emitProfileArcs(DIF);
  return false;
}

void GCOVProfiler::emitGCNO(DebugInfoFinder &DIF) {
  DenseMap<const MDNode *, raw_fd_ostream *> GcnoFiles;
  for (DebugInfoFinder::iterator I = DIF.compile_unit_begin(),
           E = DIF.compile_unit_end(); I != E; ++I) {
    // Each compile unit gets its own .gcno file. This means that whether we run
    // this pass over the original .o's as they're produced, or run it after
    // LTO, we'll generate the same .gcno files.

    DICompileUnit CU(*I);
    raw_fd_ostream *&out = GcnoFiles[CU];
    std::string ErrorInfo;
    out = new raw_fd_ostream(mangleName(CU, "gcno").c_str(), ErrorInfo,
                             raw_fd_ostream::F_Binary);
    if (!Use402Format)
      out->write("oncg*404MVLL", 12);
    else
      out->write("oncg*402MVLL", 12);
  }

  for (DebugInfoFinder::iterator SPI = DIF.subprogram_begin(),
           SPE = DIF.subprogram_end(); SPI != SPE; ++SPI) {
    DISubprogram SP(*SPI);
    raw_fd_ostream *&os = GcnoFiles[SP.getCompileUnit()];

    Function *F = SP.getFunction();
    if (!F) continue;
    GCOVFunction Func(SP, os, Use402Format);

    for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
      GCOVBlock &Block = Func.getBlock(BB);
      TerminatorInst *TI = BB->getTerminator();
      if (int successors = TI->getNumSuccessors()) {
        for (int i = 0; i != successors; ++i) {
          Block.addEdge(Func.getBlock(TI->getSuccessor(i)));
        }
      } else if (isa<ReturnInst>(TI)) {
        Block.addEdge(Func.getReturnBlock());
      }

      uint32_t Line = 0;
      for (BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; ++I) {
        const DebugLoc &Loc = I->getDebugLoc();
        if (Loc.isUnknown()) continue;
        if (Line == Loc.getLine()) continue;
        Line = Loc.getLine();
        if (SP != findSubprogram(DIScope(Loc.getScope(*Ctx)))) continue;

        GCOVLines &Lines = Block.getFile(SP.getFilename());
        Lines.addLine(Loc.getLine());
      }
    }
    Func.writeOut();
  }

  for (DenseMap<const MDNode *, raw_fd_ostream *>::iterator
           I = GcnoFiles.begin(), E = GcnoFiles.end(); I != E; ++I) {
    raw_fd_ostream *&out = I->second;
    out->write("\0\0\0\0\0\0\0\0", 8);  // EOF
    out->close();
    delete out;
  }
}

bool GCOVProfiler::emitProfileArcs(DebugInfoFinder &DIF) {
  if (DIF.subprogram_begin() == DIF.subprogram_end())
    return false;

  SmallVector<std::pair<GlobalVariable *, MDNode *>, 8> CountersBySP;
  for (DebugInfoFinder::iterator SPI = DIF.subprogram_begin(),
           SPE = DIF.subprogram_end(); SPI != SPE; ++SPI) {
    DISubprogram SP(*SPI);
    Function *F = SP.getFunction();
    if (!F) continue;

    unsigned Edges = 0;
    for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
      TerminatorInst *TI = BB->getTerminator();
      if (isa<ReturnInst>(TI))
        ++Edges;
      else
        Edges += TI->getNumSuccessors();
    }

    const ArrayType *CounterTy =
        ArrayType::get(Type::getInt64Ty(*Ctx), Edges);
    GlobalVariable *Counters =
        new GlobalVariable(*M, CounterTy, false,
                           GlobalValue::InternalLinkage,
                           Constant::getNullValue(CounterTy),
                           "__llvm_gcov_ctr", 0, false, 0);
    CountersBySP.push_back(std::make_pair(Counters, (MDNode*)SP));

    UniqueVector<BasicBlock *> ComplexEdgePreds;
    UniqueVector<BasicBlock *> ComplexEdgeSuccs;

    unsigned Edge = 0;
    for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
      TerminatorInst *TI = BB->getTerminator();
      int Successors = isa<ReturnInst>(TI) ? 1 : TI->getNumSuccessors();
      if (Successors) {
        IRBuilder<> Builder(TI);

        if (Successors == 1) {
          Value *Counter = Builder.CreateConstInBoundsGEP2_64(Counters, 0,
                                                              Edge);
          Value *Count = Builder.CreateLoad(Counter);
          Count = Builder.CreateAdd(Count,
                                    ConstantInt::get(Type::getInt64Ty(*Ctx),1));
          Builder.CreateStore(Count, Counter);
        } else if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
          Value *Sel = Builder.CreateSelect(
              BI->getCondition(),
              ConstantInt::get(Type::getInt64Ty(*Ctx), Edge),
              ConstantInt::get(Type::getInt64Ty(*Ctx), Edge + 1));
          SmallVector<Value *, 2> Idx;
          Idx.push_back(Constant::getNullValue(Type::getInt64Ty(*Ctx)));
          Idx.push_back(Sel);
          Value *Counter = Builder.CreateInBoundsGEP(Counters,
                                                     Idx.begin(), Idx.end());
          Value *Count = Builder.CreateLoad(Counter);
          Count = Builder.CreateAdd(Count,
                                    ConstantInt::get(Type::getInt64Ty(*Ctx),1));
          Builder.CreateStore(Count, Counter);
        } else {
          ComplexEdgePreds.insert(BB);
          for (int i = 0; i != Successors; ++i)
            ComplexEdgeSuccs.insert(TI->getSuccessor(i));
        }
        Edge += Successors;
      }
    }

    if (!ComplexEdgePreds.empty()) {
      GlobalVariable *EdgeTable =
          buildEdgeLookupTable(F, Counters,
                               ComplexEdgePreds, ComplexEdgeSuccs);
      GlobalVariable *EdgeState = getEdgeStateValue();

      const Type *Int32Ty = Type::getInt32Ty(*Ctx);
      for (int i = 0, e = ComplexEdgePreds.size(); i != e; ++i) {
        IRBuilder<> Builder(ComplexEdgePreds[i+1]->getTerminator());
        Builder.CreateStore(ConstantInt::get(Int32Ty, i), EdgeState);
      }
      for (int i = 0, e = ComplexEdgeSuccs.size(); i != e; ++i) {
        // call runtime to perform increment
        IRBuilder<> Builder(ComplexEdgeSuccs[i+1]->getFirstNonPHI());
        Value *CounterPtrArray =
            Builder.CreateConstInBoundsGEP2_64(EdgeTable, 0,
                                               i * ComplexEdgePreds.size());
        Builder.CreateCall2(getIncrementIndirectCounterFunc(),
                            EdgeState, CounterPtrArray);
        // clear the predecessor number
        Builder.CreateStore(ConstantInt::get(Int32Ty, 0xffffffff), EdgeState);
      }
    }
  }

  insertCounterWriteout(DIF, CountersBySP);

  return true;
}

// All edges with successors that aren't branches are "complex", because it
// requires complex logic to pick which counter to update.
GlobalVariable *GCOVProfiler::buildEdgeLookupTable(
    Function *F,
    GlobalVariable *Counters,
    const UniqueVector<BasicBlock *> &Preds,
    const UniqueVector<BasicBlock *> &Succs) {
  // TODO: support invoke, threads. We rely on the fact that nothing can modify
  // the whole-Module pred edge# between the time we set it and the time we next
  // read it. Threads and invoke make this untrue.

  // emit [(succs * preds) x i64*], logically [succ x [pred x i64*]].
  const Type *Int64PtrTy = Type::getInt64PtrTy(*Ctx);
  const ArrayType *EdgeTableTy = ArrayType::get(
      Int64PtrTy, Succs.size() * Preds.size());

  Constant **EdgeTable = new Constant*[Succs.size() * Preds.size()];
  Constant *NullValue = Constant::getNullValue(Int64PtrTy);
  for (int i = 0, ie = Succs.size() * Preds.size(); i != ie; ++i)
    EdgeTable[i] = NullValue;

  unsigned Edge = 0;
  for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
    TerminatorInst *TI = BB->getTerminator();
    int Successors = isa<ReturnInst>(TI) ? 1 : TI->getNumSuccessors();
    if (Successors > 1 && !isa<BranchInst>(TI) && !isa<ReturnInst>(TI)) {
      for (int i = 0; i != Successors; ++i) {
        BasicBlock *Succ = TI->getSuccessor(i);
        IRBuilder<> builder(Succ);
        Value *Counter = builder.CreateConstInBoundsGEP2_64(Counters, 0,
                                                            Edge + i);
        EdgeTable[((Succs.idFor(Succ)-1) * Preds.size()) +
                  (Preds.idFor(BB)-1)] = cast<Constant>(Counter);
      }
    }
    Edge += Successors;
  }

  GlobalVariable *EdgeTableGV =
      new GlobalVariable(
          *M, EdgeTableTy, true, GlobalValue::InternalLinkage,
          ConstantArray::get(EdgeTableTy,
                             &EdgeTable[0], Succs.size() * Preds.size()),
          "__llvm_gcda_edge_table");
  EdgeTableGV->setUnnamedAddr(true);
  return EdgeTableGV;
}

Constant *GCOVProfiler::getStartFileFunc() {
  const Type *Args[] = { Type::getInt8PtrTy(*Ctx) };
  const FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx),
                                              Args, false);
  return M->getOrInsertFunction("llvm_gcda_start_file", FTy);
}

Constant *GCOVProfiler::getIncrementIndirectCounterFunc() {
  const Type *Args[] = {
    Type::getInt32PtrTy(*Ctx),                  // uint32_t *predecessor
    Type::getInt64PtrTy(*Ctx)->getPointerTo(),  // uint64_t **state_table_row
  };
  const FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx),
                                              Args, false);
  return M->getOrInsertFunction("llvm_gcda_increment_indirect_counter", FTy);
}

Constant *GCOVProfiler::getEmitFunctionFunc() {
  const Type *Args[2] = {
    Type::getInt32Ty(*Ctx),    // uint32_t ident
    Type::getInt8PtrTy(*Ctx),  // const char *function_name
  };
  const FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx),
                                              Args, false);
  return M->getOrInsertFunction("llvm_gcda_emit_function", FTy);
}

Constant *GCOVProfiler::getEmitArcsFunc() {
  const Type *Args[] = {
    Type::getInt32Ty(*Ctx),     // uint32_t num_counters
    Type::getInt64PtrTy(*Ctx),  // uint64_t *counters
  };
  const FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx),
                                              Args, false);
  return M->getOrInsertFunction("llvm_gcda_emit_arcs", FTy);
}

Constant *GCOVProfiler::getEndFileFunc() {
  const FunctionType *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
  return M->getOrInsertFunction("llvm_gcda_end_file", FTy);
}

GlobalVariable *GCOVProfiler::getEdgeStateValue() {
  GlobalVariable *GV = M->getGlobalVariable("__llvm_gcov_global_state_pred");
  if (!GV) {
    GV = new GlobalVariable(*M, Type::getInt32Ty(*Ctx), false,
                            GlobalValue::InternalLinkage,
                            ConstantInt::get(Type::getInt32Ty(*Ctx),
                                             0xffffffff),
                            "__llvm_gcov_global_state_pred");
    GV->setUnnamedAddr(true);
  }
  return GV;
}

void GCOVProfiler::insertCounterWriteout(
    DebugInfoFinder &DIF,
    SmallVector<std::pair<GlobalVariable *, MDNode *>, 8> &CountersBySP) {
  const FunctionType *WriteoutFTy =
      FunctionType::get(Type::getVoidTy(*Ctx), false);
  Function *WriteoutF = Function::Create(WriteoutFTy,
                                         GlobalValue::InternalLinkage,
                                         "__llvm_gcov_writeout", M);
  WriteoutF->setUnnamedAddr(true);
  BasicBlock *BB = BasicBlock::Create(*Ctx, "", WriteoutF);
  IRBuilder<> Builder(BB);

  Constant *StartFile = getStartFileFunc();
  Constant *EmitFunction = getEmitFunctionFunc();
  Constant *EmitArcs = getEmitArcsFunc();
  Constant *EndFile = getEndFileFunc();

  for (DebugInfoFinder::iterator CUI = DIF.compile_unit_begin(),
           CUE = DIF.compile_unit_end(); CUI != CUE; ++CUI) {
    DICompileUnit compile_unit(*CUI);
    std::string FilenameGcda = mangleName(compile_unit, "gcda");
    Builder.CreateCall(StartFile,
                       Builder.CreateGlobalStringPtr(FilenameGcda));
    for (SmallVector<std::pair<GlobalVariable *, MDNode *>, 8>::iterator
             I = CountersBySP.begin(), E = CountersBySP.end();
         I != E; ++I) {
      DISubprogram SP(I->second);
      intptr_t ident = reinterpret_cast<intptr_t>(I->second);
      Builder.CreateCall2(EmitFunction,
                          ConstantInt::get(Type::getInt32Ty(*Ctx), ident),
                          Builder.CreateGlobalStringPtr(SP.getName()));
                                                        
      GlobalVariable *GV = I->first;
      unsigned Arcs =
          cast<ArrayType>(GV->getType()->getElementType())->getNumElements();
      Builder.CreateCall2(EmitArcs,
                          ConstantInt::get(Type::getInt32Ty(*Ctx), Arcs),
                          Builder.CreateConstGEP2_64(GV, 0, 0));
    }
    Builder.CreateCall(EndFile);
  }
  Builder.CreateRetVoid();

  InsertProfilingShutdownCall(WriteoutF, M);
}
