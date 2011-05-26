//===-- llvm/CodeGen/DwarfCompileUnit.cpp - Dwarf Compile Unit ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing dwarf compile unit.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "dwarfdebug"

#include "DwarfCompileUnit.h"
#include "DwarfDebug.h"
#include "llvm/Constants.h"
#include "llvm/Analysis/DIBuilder.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

/// CompileUnit - Compile unit constructor.
CompileUnit::CompileUnit(unsigned I, DIE *D, AsmPrinter *A, DwarfDebug *DW)
  : ID(I), CUDie(D), Asm(A), DD(DW), IndexTyDie(0) {
  DIEIntegerOne = new (DIEValueAllocator) DIEInteger(1);
}

/// ~CompileUnit - Destructor for compile unit.
CompileUnit::~CompileUnit() {
  for (unsigned j = 0, M = DIEBlocks.size(); j < M; ++j)
    DIEBlocks[j]->~DIEBlock();
}

/// createDIEEntry - Creates a new DIEEntry to be a proxy for a debug
/// information entry.
DIEEntry *CompileUnit::createDIEEntry(DIE *Entry) {
  DIEEntry *Value = new (DIEValueAllocator) DIEEntry(Entry);
  return Value;
}

/// addUInt - Add an unsigned integer attribute data and value.
///
void CompileUnit::addUInt(DIE *Die, unsigned Attribute,
                          unsigned Form, uint64_t Integer) {
  if (!Form) Form = DIEInteger::BestForm(false, Integer);
  DIEValue *Value = Integer == 1 ?
    DIEIntegerOne : new (DIEValueAllocator) DIEInteger(Integer);
  Die->addValue(Attribute, Form, Value);
}

/// addSInt - Add an signed integer attribute data and value.
///
void CompileUnit::addSInt(DIE *Die, unsigned Attribute,
                          unsigned Form, int64_t Integer) {
  if (!Form) Form = DIEInteger::BestForm(true, Integer);
  DIEValue *Value = new (DIEValueAllocator) DIEInteger(Integer);
  Die->addValue(Attribute, Form, Value);
}

/// addString - Add a string attribute data and value. DIEString only
/// keeps string reference.
void CompileUnit::addString(DIE *Die, unsigned Attribute, unsigned Form,
                            StringRef String) {
  DIEValue *Value = new (DIEValueAllocator) DIEString(String);
  Die->addValue(Attribute, Form, Value);
}

/// addLabel - Add a Dwarf label attribute data and value.
///
void CompileUnit::addLabel(DIE *Die, unsigned Attribute, unsigned Form,
                           const MCSymbol *Label) {
  DIEValue *Value = new (DIEValueAllocator) DIELabel(Label);
  Die->addValue(Attribute, Form, Value);
}

/// addDelta - Add a label delta attribute data and value.
///
void CompileUnit::addDelta(DIE *Die, unsigned Attribute, unsigned Form,
                           const MCSymbol *Hi, const MCSymbol *Lo) {
  DIEValue *Value = new (DIEValueAllocator) DIEDelta(Hi, Lo);
  Die->addValue(Attribute, Form, Value);
}

/// addDIEEntry - Add a DIE attribute data and value.
///
void CompileUnit::addDIEEntry(DIE *Die, unsigned Attribute, unsigned Form,
                              DIE *Entry) {
  Die->addValue(Attribute, Form, createDIEEntry(Entry));
}


/// addBlock - Add block data.
///
void CompileUnit::addBlock(DIE *Die, unsigned Attribute, unsigned Form,
                           DIEBlock *Block) {
  Block->ComputeSize(Asm);
  DIEBlocks.push_back(Block); // Memoize so we can call the destructor later on.
  Die->addValue(Attribute, Block->BestForm(), Block);
}

/// addSourceLine - Add location information to specified debug information
/// entry.
void CompileUnit::addSourceLine(DIE *Die, DIVariable V) {
  // Verify variable.
  if (!V.Verify())
    return;
  
  unsigned Line = V.getLineNumber();
  if (Line == 0)
    return;
  unsigned FileID = DD->GetOrCreateSourceID(V.getContext().getFilename(),
                                            V.getContext().getDirectory());
  assert(FileID && "Invalid file id");
  addUInt(Die, dwarf::DW_AT_decl_file, 0, FileID);
  addUInt(Die, dwarf::DW_AT_decl_line, 0, Line);
}

/// addSourceLine - Add location information to specified debug information
/// entry.
void CompileUnit::addSourceLine(DIE *Die, DIGlobalVariable G) {
  // Verify global variable.
  if (!G.Verify())
    return;

  unsigned Line = G.getLineNumber();
  if (Line == 0)
    return;
  unsigned FileID = DD->GetOrCreateSourceID(G.getContext().getFilename(),
                                            G.getContext().getDirectory());
  assert(FileID && "Invalid file id");
  addUInt(Die, dwarf::DW_AT_decl_file, 0, FileID);
  addUInt(Die, dwarf::DW_AT_decl_line, 0, Line);
}

/// addSourceLine - Add location information to specified debug information
/// entry.
void CompileUnit::addSourceLine(DIE *Die, DISubprogram SP) {
  // Verify subprogram.
  if (!SP.Verify())
    return;
  // If the line number is 0, don't add it.
  if (SP.getLineNumber() == 0)
    return;

  unsigned Line = SP.getLineNumber();
  if (!SP.getContext().Verify())
    return;
  unsigned FileID = DD->GetOrCreateSourceID(SP.getFilename(), SP.getDirectory());
  assert(FileID && "Invalid file id");
  addUInt(Die, dwarf::DW_AT_decl_file, 0, FileID);
  addUInt(Die, dwarf::DW_AT_decl_line, 0, Line);
}

/// addSourceLine - Add location information to specified debug information
/// entry.
void CompileUnit::addSourceLine(DIE *Die, DIType Ty) {
  // Verify type.
  if (!Ty.Verify())
    return;

  unsigned Line = Ty.getLineNumber();
  if (Line == 0 || !Ty.getContext().Verify())
    return;
  unsigned FileID = DD->GetOrCreateSourceID(Ty.getFilename(), Ty.getDirectory());
  assert(FileID && "Invalid file id");
  addUInt(Die, dwarf::DW_AT_decl_file, 0, FileID);
  addUInt(Die, dwarf::DW_AT_decl_line, 0, Line);
}

/// addSourceLine - Add location information to specified debug information
/// entry.
void CompileUnit::addSourceLine(DIE *Die, DINameSpace NS) {
  // Verify namespace.
  if (!NS.Verify())
    return;

  unsigned Line = NS.getLineNumber();
  if (Line == 0)
    return;
  StringRef FN = NS.getFilename();

  unsigned FileID = DD->GetOrCreateSourceID(FN, NS.getDirectory());
  assert(FileID && "Invalid file id");
  addUInt(Die, dwarf::DW_AT_decl_file, 0, FileID);
  addUInt(Die, dwarf::DW_AT_decl_line, 0, Line);
}

/// addVariableAddress - Add DW_AT_location attribute for a 
/// DbgVariable based on provided MachineLocation.
void CompileUnit::addVariableAddress(DbgVariable *&DV, DIE *Die, 
                                     MachineLocation Location) {
  if (DV->variableHasComplexAddress())
    addComplexAddress(DV, Die, dwarf::DW_AT_location, Location);
  else if (DV->isBlockByrefVariable())
    addBlockByrefAddress(DV, Die, dwarf::DW_AT_location, Location);
  else
    addAddress(Die, dwarf::DW_AT_location, Location);
}

/// addRegisterOp - Add register operand.
void CompileUnit::addRegisterOp(DIE *TheDie, unsigned Reg) {
  const TargetRegisterInfo *RI = Asm->TM.getRegisterInfo();
  unsigned DWReg = RI->getDwarfRegNum(Reg, false);
  if (DWReg < 32)
    addUInt(TheDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_reg0 + DWReg);
  else {
    addUInt(TheDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_regx);
    addUInt(TheDie, 0, dwarf::DW_FORM_udata, DWReg);
  }
}

/// addRegisterOffset - Add register offset.
void CompileUnit::addRegisterOffset(DIE *TheDie, unsigned Reg,
                                    int64_t Offset) {
  const TargetRegisterInfo *RI = Asm->TM.getRegisterInfo();
  unsigned DWReg = RI->getDwarfRegNum(Reg, false);
  const TargetRegisterInfo *TRI = Asm->TM.getRegisterInfo();
  if (Reg == TRI->getFrameRegister(*Asm->MF))
    // If variable offset is based in frame register then use fbreg.
    addUInt(TheDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_fbreg);
  else if (DWReg < 32)
    addUInt(TheDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_breg0 + DWReg);
  else {
    addUInt(TheDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_bregx);
    addUInt(TheDie, 0, dwarf::DW_FORM_udata, DWReg);
  }
  addSInt(TheDie, 0, dwarf::DW_FORM_sdata, Offset);
}

/// addAddress - Add an address attribute to a die based on the location
/// provided.
void CompileUnit::addAddress(DIE *Die, unsigned Attribute,
                             const MachineLocation &Location) {
  DIEBlock *Block = new (DIEValueAllocator) DIEBlock();

  if (Location.isReg())
    addRegisterOp(Block, Location.getReg());
  else
    addRegisterOffset(Block, Location.getReg(), Location.getOffset());

  // Now attach the location information to the DIE.
  addBlock(Die, Attribute, 0, Block);
}

/// addComplexAddress - Start with the address based on the location provided,
/// and generate the DWARF information necessary to find the actual variable
/// given the extra address information encoded in the DIVariable, starting from
/// the starting location.  Add the DWARF information to the die.
///
void CompileUnit::addComplexAddress(DbgVariable *&DV, DIE *Die,
                                    unsigned Attribute,
                                    const MachineLocation &Location) {
  DIEBlock *Block = new (DIEValueAllocator) DIEBlock();
  unsigned N = DV->getNumAddrElements();
  unsigned i = 0;
  if (Location.isReg()) {
    if (N >= 2 && DV->getAddrElement(0) == DIBuilder::OpPlus) {
      // If first address element is OpPlus then emit
      // DW_OP_breg + Offset instead of DW_OP_reg + Offset.
      addRegisterOffset(Block, Location.getReg(), DV->getAddrElement(1));
      i = 2;
    } else
      addRegisterOp(Block, Location.getReg());
  }
  else
    addRegisterOffset(Block, Location.getReg(), Location.getOffset());

  for (;i < N; ++i) {
    uint64_t Element = DV->getAddrElement(i);
    if (Element == DIBuilder::OpPlus) {
      addUInt(Block, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_plus_uconst);
      addUInt(Block, 0, dwarf::DW_FORM_udata, DV->getAddrElement(++i));
    } else if (Element == DIBuilder::OpDeref) {
      addUInt(Block, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_deref);
    } else llvm_unreachable("unknown DIBuilder Opcode");
  }

  // Now attach the location information to the DIE.
  addBlock(Die, Attribute, 0, Block);
}

/* Byref variables, in Blocks, are declared by the programmer as "SomeType
   VarName;", but the compiler creates a __Block_byref_x_VarName struct, and
   gives the variable VarName either the struct, or a pointer to the struct, as
   its type.  This is necessary for various behind-the-scenes things the
   compiler needs to do with by-reference variables in Blocks.

   However, as far as the original *programmer* is concerned, the variable
   should still have type 'SomeType', as originally declared.

   The function getBlockByrefType dives into the __Block_byref_x_VarName
   struct to find the original type of the variable, which is then assigned to
   the variable's Debug Information Entry as its real type.  So far, so good.
   However now the debugger will expect the variable VarName to have the type
   SomeType.  So we need the location attribute for the variable to be an
   expression that explains to the debugger how to navigate through the
   pointers and struct to find the actual variable of type SomeType.

   The following function does just that.  We start by getting
   the "normal" location for the variable. This will be the location
   of either the struct __Block_byref_x_VarName or the pointer to the
   struct __Block_byref_x_VarName.

   The struct will look something like:

   struct __Block_byref_x_VarName {
     ... <various fields>
     struct __Block_byref_x_VarName *forwarding;
     ... <various other fields>
     SomeType VarName;
     ... <maybe more fields>
   };

   If we are given the struct directly (as our starting point) we
   need to tell the debugger to:

   1).  Add the offset of the forwarding field.

   2).  Follow that pointer to get the real __Block_byref_x_VarName
   struct to use (the real one may have been copied onto the heap).

   3).  Add the offset for the field VarName, to find the actual variable.

   If we started with a pointer to the struct, then we need to
   dereference that pointer first, before the other steps.
   Translating this into DWARF ops, we will need to append the following
   to the current location description for the variable:

   DW_OP_deref                    -- optional, if we start with a pointer
   DW_OP_plus_uconst <forward_fld_offset>
   DW_OP_deref
   DW_OP_plus_uconst <varName_fld_offset>

   That is what this function does.  */

/// addBlockByrefAddress - Start with the address based on the location
/// provided, and generate the DWARF information necessary to find the
/// actual Block variable (navigating the Block struct) based on the
/// starting location.  Add the DWARF information to the die.  For
/// more information, read large comment just above here.
///
void CompileUnit::addBlockByrefAddress(DbgVariable *&DV, DIE *Die,
                                       unsigned Attribute,
                                       const MachineLocation &Location) {
  DIType Ty = DV->getType();
  DIType TmpTy = Ty;
  unsigned Tag = Ty.getTag();
  bool isPointer = false;

  StringRef varName = DV->getName();

  if (Tag == dwarf::DW_TAG_pointer_type) {
    DIDerivedType DTy = DIDerivedType(Ty);
    TmpTy = DTy.getTypeDerivedFrom();
    isPointer = true;
  }

  DICompositeType blockStruct = DICompositeType(TmpTy);

  // Find the __forwarding field and the variable field in the __Block_byref
  // struct.
  DIArray Fields = blockStruct.getTypeArray();
  DIDescriptor varField = DIDescriptor();
  DIDescriptor forwardingField = DIDescriptor();

  for (unsigned i = 0, N = Fields.getNumElements(); i < N; ++i) {
    DIDescriptor Element = Fields.getElement(i);
    DIDerivedType DT = DIDerivedType(Element);
    StringRef fieldName = DT.getName();
    if (fieldName == "__forwarding")
      forwardingField = Element;
    else if (fieldName == varName)
      varField = Element;
  }

  // Get the offsets for the forwarding field and the variable field.
  unsigned forwardingFieldOffset =
    DIDerivedType(forwardingField).getOffsetInBits() >> 3;
  unsigned varFieldOffset =
    DIDerivedType(varField).getOffsetInBits() >> 3;

  // Decode the original location, and use that as the start of the byref
  // variable's location.
  const TargetRegisterInfo *RI = Asm->TM.getRegisterInfo();
  unsigned Reg = RI->getDwarfRegNum(Location.getReg(), false);
  DIEBlock *Block = new (DIEValueAllocator) DIEBlock();

  if (Location.isReg()) {
    if (Reg < 32)
      addUInt(Block, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_reg0 + Reg);
    else {
      addUInt(Block, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_regx);
      addUInt(Block, 0, dwarf::DW_FORM_udata, Reg);
    }
  } else {
    if (Reg < 32)
      addUInt(Block, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_breg0 + Reg);
    else {
      addUInt(Block, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_bregx);
      addUInt(Block, 0, dwarf::DW_FORM_udata, Reg);
    }

    addUInt(Block, 0, dwarf::DW_FORM_sdata, Location.getOffset());
  }

  // If we started with a pointer to the __Block_byref... struct, then
  // the first thing we need to do is dereference the pointer (DW_OP_deref).
  if (isPointer)
    addUInt(Block, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_deref);

  // Next add the offset for the '__forwarding' field:
  // DW_OP_plus_uconst ForwardingFieldOffset.  Note there's no point in
  // adding the offset if it's 0.
  if (forwardingFieldOffset > 0) {
    addUInt(Block, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_plus_uconst);
    addUInt(Block, 0, dwarf::DW_FORM_udata, forwardingFieldOffset);
  }

  // Now dereference the __forwarding field to get to the real __Block_byref
  // struct:  DW_OP_deref.
  addUInt(Block, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_deref);

  // Now that we've got the real __Block_byref... struct, add the offset
  // for the variable's field to get to the location of the actual variable:
  // DW_OP_plus_uconst varFieldOffset.  Again, don't add if it's 0.
  if (varFieldOffset > 0) {
    addUInt(Block, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_plus_uconst);
    addUInt(Block, 0, dwarf::DW_FORM_udata, varFieldOffset);
  }

  // Now attach the location information to the DIE.
  addBlock(Die, Attribute, 0, Block);
}

/// addConstantValue - Add constant value entry in variable DIE.
bool CompileUnit::addConstantValue(DIE *Die, const MachineOperand &MO) {
  assert (MO.isImm() && "Invalid machine operand!");
  DIEBlock *Block = new (DIEValueAllocator) DIEBlock();
  unsigned Imm = MO.getImm();
  addUInt(Block, 0, dwarf::DW_FORM_udata, Imm);
  addBlock(Die, dwarf::DW_AT_const_value, 0, Block);
  return true;
}

/// addConstantFPValue - Add constant value entry in variable DIE.
bool CompileUnit::addConstantFPValue(DIE *Die, const MachineOperand &MO) {
  assert (MO.isFPImm() && "Invalid machine operand!");
  DIEBlock *Block = new (DIEValueAllocator) DIEBlock();
  APFloat FPImm = MO.getFPImm()->getValueAPF();

  // Get the raw data form of the floating point.
  const APInt FltVal = FPImm.bitcastToAPInt();
  const char *FltPtr = (const char*)FltVal.getRawData();

  int NumBytes = FltVal.getBitWidth() / 8; // 8 bits per byte.
  bool LittleEndian = Asm->getTargetData().isLittleEndian();
  int Incr = (LittleEndian ? 1 : -1);
  int Start = (LittleEndian ? 0 : NumBytes - 1);
  int Stop = (LittleEndian ? NumBytes : -1);

  // Output the constant to DWARF one byte at a time.
  for (; Start != Stop; Start += Incr)
    addUInt(Block, 0, dwarf::DW_FORM_data1,
            (unsigned char)0xFF & FltPtr[Start]);

  addBlock(Die, dwarf::DW_AT_const_value, 0, Block);
  return true;
}

/// addConstantValue - Add constant value entry in variable DIE.
bool CompileUnit::addConstantValue(DIE *Die, ConstantInt *CI,
                                   bool Unsigned) {
  if (CI->getBitWidth() <= 64) {
    if (Unsigned)
      addUInt(Die, dwarf::DW_AT_const_value, dwarf::DW_FORM_udata,
              CI->getZExtValue());
    else
      addSInt(Die, dwarf::DW_AT_const_value, dwarf::DW_FORM_sdata,
              CI->getSExtValue());
    return true;
  }

  DIEBlock *Block = new (DIEValueAllocator) DIEBlock();

  // Get the raw data form of the large APInt.
  const APInt Val = CI->getValue();
  const char *Ptr = (const char*)Val.getRawData();

  int NumBytes = Val.getBitWidth() / 8; // 8 bits per byte.
  bool LittleEndian = Asm->getTargetData().isLittleEndian();
  int Incr = (LittleEndian ? 1 : -1);
  int Start = (LittleEndian ? 0 : NumBytes - 1);
  int Stop = (LittleEndian ? NumBytes : -1);

  // Output the constant to DWARF one byte at a time.
  for (; Start != Stop; Start += Incr)
    addUInt(Block, 0, dwarf::DW_FORM_data1,
            (unsigned char)0xFF & Ptr[Start]);

  addBlock(Die, dwarf::DW_AT_const_value, 0, Block);
  return true;
}

/// addTemplateParams - Add template parameters in buffer.
void CompileUnit::addTemplateParams(DIE &Buffer, DIArray TParams) {
  // Add template parameters.
  for (unsigned i = 0, e = TParams.getNumElements(); i != e; ++i) {
    DIDescriptor Element = TParams.getElement(i);
    if (Element.isTemplateTypeParameter())
      Buffer.addChild(getOrCreateTemplateTypeParameterDIE(
                        DITemplateTypeParameter(Element)));
    else if (Element.isTemplateValueParameter())
      Buffer.addChild(getOrCreateTemplateValueParameterDIE(
                        DITemplateValueParameter(Element)));
  }

}
/// addToContextOwner - Add Die into the list of its context owner's children.
void CompileUnit::addToContextOwner(DIE *Die, DIDescriptor Context) {
  if (Context.isType()) {
    DIE *ContextDIE = getOrCreateTypeDIE(DIType(Context));
    ContextDIE->addChild(Die);
  } else if (Context.isNameSpace()) {
    DIE *ContextDIE = getOrCreateNameSpace(DINameSpace(Context));
    ContextDIE->addChild(Die);
  } else if (Context.isSubprogram()) {
    DIE *ContextDIE = DD->createSubprogramDIE(DISubprogram(Context));
    ContextDIE->addChild(Die);
  } else if (DIE *ContextDIE = getDIE(Context))
    ContextDIE->addChild(Die);
  else
    addDie(Die);
}

/// getOrCreateTypeDIE - Find existing DIE or create new DIE for the
/// given DIType.
DIE *CompileUnit::getOrCreateTypeDIE(DIType Ty) {
  DIE *TyDIE = getDIE(Ty);
  if (TyDIE)
    return TyDIE;

  // Create new type.
  TyDIE = new DIE(dwarf::DW_TAG_base_type);
  insertDIE(Ty, TyDIE);
  if (Ty.isBasicType())
    constructTypeDIE(*TyDIE, DIBasicType(Ty));
  else if (Ty.isCompositeType())
    constructTypeDIE(*TyDIE, DICompositeType(Ty));
  else {
    assert(Ty.isDerivedType() && "Unknown kind of DIType");
    constructTypeDIE(*TyDIE, DIDerivedType(Ty));
  }

  addToContextOwner(TyDIE, Ty.getContext());
  return TyDIE;
}

/// addType - Add a new type attribute to the specified entity.
void CompileUnit::addType(DIE *Entity, DIType Ty) {
  if (!Ty.Verify())
    return;

  // Check for pre-existence.
  DIEEntry *Entry = getDIEEntry(Ty);
  // If it exists then use the existing value.
  if (Entry) {
    Entity->addValue(dwarf::DW_AT_type, dwarf::DW_FORM_ref4, Entry);
    return;
  }

  // Construct type.
  DIE *Buffer = getOrCreateTypeDIE(Ty);

  // Set up proxy.
  Entry = createDIEEntry(Buffer);
  insertDIEEntry(Ty, Entry);

  Entity->addValue(dwarf::DW_AT_type, dwarf::DW_FORM_ref4, Entry);
}

/// addPubTypes - Add type for pubtypes section.
void CompileUnit::addPubTypes(DISubprogram SP) {
  DICompositeType SPTy = SP.getType();
  unsigned SPTag = SPTy.getTag();
  if (SPTag != dwarf::DW_TAG_subroutine_type)
    return;

  DIArray Args = SPTy.getTypeArray();
  for (unsigned i = 0, e = Args.getNumElements(); i != e; ++i) {
    DIType ATy(Args.getElement(i));
    if (!ATy.Verify())
      continue;
    DICompositeType CATy = getDICompositeType(ATy);
    if (DIDescriptor(CATy).Verify() && !CATy.getName().empty()
        && !CATy.isForwardDecl()) {
      if (DIEEntry *Entry = getDIEEntry(CATy))
        addGlobalType(CATy.getName(), Entry->getEntry());
    }
  }
}

/// constructTypeDIE - Construct basic type die from DIBasicType.
void CompileUnit::constructTypeDIE(DIE &Buffer, DIBasicType BTy) {
  // Get core information.
  StringRef Name = BTy.getName();
  Buffer.setTag(dwarf::DW_TAG_base_type);
  addUInt(&Buffer, dwarf::DW_AT_encoding,  dwarf::DW_FORM_data1,
          BTy.getEncoding());

  // Add name if not anonymous or intermediate type.
  if (!Name.empty())
    addString(&Buffer, dwarf::DW_AT_name, dwarf::DW_FORM_string, Name);
  uint64_t Size = BTy.getSizeInBits() >> 3;
  addUInt(&Buffer, dwarf::DW_AT_byte_size, 0, Size);
}

/// constructTypeDIE - Construct derived type die from DIDerivedType.
void CompileUnit::constructTypeDIE(DIE &Buffer, DIDerivedType DTy) {
  // Get core information.
  StringRef Name = DTy.getName();
  uint64_t Size = DTy.getSizeInBits() >> 3;
  unsigned Tag = DTy.getTag();

  // FIXME - Workaround for templates.
  if (Tag == dwarf::DW_TAG_inheritance) Tag = dwarf::DW_TAG_reference_type;

  Buffer.setTag(Tag);

  // Map to main type, void will not have a type.
  DIType FromTy = DTy.getTypeDerivedFrom();
  addType(&Buffer, FromTy);

  // Add name if not anonymous or intermediate type.
  if (!Name.empty())
    addString(&Buffer, dwarf::DW_AT_name, dwarf::DW_FORM_string, Name);

  // Add size if non-zero (derived types might be zero-sized.)
  if (Size)
    addUInt(&Buffer, dwarf::DW_AT_byte_size, 0, Size);

  // Add source line info if available and TyDesc is not a forward declaration.
  if (!DTy.isForwardDecl())
    addSourceLine(&Buffer, DTy);
}

/// constructTypeDIE - Construct type DIE from DICompositeType.
void CompileUnit::constructTypeDIE(DIE &Buffer, DICompositeType CTy) {
  // Get core information.
  StringRef Name = CTy.getName();

  uint64_t Size = CTy.getSizeInBits() >> 3;
  unsigned Tag = CTy.getTag();
  Buffer.setTag(Tag);

  switch (Tag) {
  case dwarf::DW_TAG_vector_type:
  case dwarf::DW_TAG_array_type:
    constructArrayTypeDIE(Buffer, &CTy);
    break;
  case dwarf::DW_TAG_enumeration_type: {
    DIArray Elements = CTy.getTypeArray();

    // Add enumerators to enumeration type.
    for (unsigned i = 0, N = Elements.getNumElements(); i < N; ++i) {
      DIE *ElemDie = NULL;
      DIDescriptor Enum(Elements.getElement(i));
      if (Enum.isEnumerator()) {
        ElemDie = constructEnumTypeDIE(DIEnumerator(Enum));
        Buffer.addChild(ElemDie);
      }
    }
  }
    break;
  case dwarf::DW_TAG_subroutine_type: {
    // Add return type.
    DIArray Elements = CTy.getTypeArray();
    DIDescriptor RTy = Elements.getElement(0);
    addType(&Buffer, DIType(RTy));

    bool isPrototyped = true;
    // Add arguments.
    for (unsigned i = 1, N = Elements.getNumElements(); i < N; ++i) {
      DIDescriptor Ty = Elements.getElement(i);
      if (Ty.isUnspecifiedParameter()) {
        DIE *Arg = new DIE(dwarf::DW_TAG_unspecified_parameters);
        Buffer.addChild(Arg);
        isPrototyped = false;
      } else {
        DIE *Arg = new DIE(dwarf::DW_TAG_formal_parameter);
        addType(Arg, DIType(Ty));
        Buffer.addChild(Arg);
      }
    }
    // Add prototype flag.
    if (isPrototyped)
      addUInt(&Buffer, dwarf::DW_AT_prototyped, dwarf::DW_FORM_flag, 1);
  }
    break;
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_class_type: {
    // Add elements to structure type.
    DIArray Elements = CTy.getTypeArray();

    // A forward struct declared type may not have elements available.
    unsigned N = Elements.getNumElements();
    if (N == 0)
      break;

    // Add elements to structure type.
    for (unsigned i = 0; i < N; ++i) {
      DIDescriptor Element = Elements.getElement(i);
      DIE *ElemDie = NULL;
      if (Element.isSubprogram()) {
        DISubprogram SP(Element);
        ElemDie = DD->createSubprogramDIE(DISubprogram(Element));
        if (SP.isProtected())
          addUInt(ElemDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_flag,
                  dwarf::DW_ACCESS_protected);
        else if (SP.isPrivate())
          addUInt(ElemDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_flag,
                  dwarf::DW_ACCESS_private);
        else 
          addUInt(ElemDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_flag,
            dwarf::DW_ACCESS_public);
        if (SP.isExplicit())
          addUInt(ElemDie, dwarf::DW_AT_explicit, dwarf::DW_FORM_flag, 1);
      }
      else if (Element.isVariable()) {
        DIVariable DV(Element);
        ElemDie = new DIE(dwarf::DW_TAG_variable);
        addString(ElemDie, dwarf::DW_AT_name, dwarf::DW_FORM_string,
                  DV.getName());
        addType(ElemDie, DV.getType());
        addUInt(ElemDie, dwarf::DW_AT_declaration, dwarf::DW_FORM_flag, 1);
        addUInt(ElemDie, dwarf::DW_AT_external, dwarf::DW_FORM_flag, 1);
        addSourceLine(ElemDie, DV);
      } else if (Element.isDerivedType())
        ElemDie = createMemberDIE(DIDerivedType(Element));
      else
        continue;
      Buffer.addChild(ElemDie);
    }

    if (CTy.isAppleBlockExtension())
      addUInt(&Buffer, dwarf::DW_AT_APPLE_block, dwarf::DW_FORM_flag, 1);

    unsigned RLang = CTy.getRunTimeLang();
    if (RLang)
      addUInt(&Buffer, dwarf::DW_AT_APPLE_runtime_class,
              dwarf::DW_FORM_data1, RLang);

    DICompositeType ContainingType = CTy.getContainingType();
    if (DIDescriptor(ContainingType).isCompositeType())
      addDIEEntry(&Buffer, dwarf::DW_AT_containing_type, dwarf::DW_FORM_ref4,
                  getOrCreateTypeDIE(DIType(ContainingType)));
    else {
      DIDescriptor Context = CTy.getContext();
      addToContextOwner(&Buffer, Context);
    }

    if (CTy.isObjcClassComplete())
      addUInt(&Buffer, dwarf::DW_AT_APPLE_objc_complete_type,
              dwarf::DW_FORM_flag, 1);

    if (Tag == dwarf::DW_TAG_class_type) 
      addTemplateParams(Buffer, CTy.getTemplateParams());

    break;
  }
  default:
    break;
  }

  // Add name if not anonymous or intermediate type.
  if (!Name.empty())
    addString(&Buffer, dwarf::DW_AT_name, dwarf::DW_FORM_string, Name);

  if (Tag == dwarf::DW_TAG_enumeration_type || Tag == dwarf::DW_TAG_class_type
      || Tag == dwarf::DW_TAG_structure_type || Tag == dwarf::DW_TAG_union_type)
    {
    // Add size if non-zero (derived types might be zero-sized.)
    if (Size)
      addUInt(&Buffer, dwarf::DW_AT_byte_size, 0, Size);
    else {
      // Add zero size if it is not a forward declaration.
      if (CTy.isForwardDecl())
        addUInt(&Buffer, dwarf::DW_AT_declaration, dwarf::DW_FORM_flag, 1);
      else
        addUInt(&Buffer, dwarf::DW_AT_byte_size, 0, 0);
    }

    // Add source line info if available.
    if (!CTy.isForwardDecl())
      addSourceLine(&Buffer, CTy);
  }
}

/// getOrCreateTemplateTypeParameterDIE - Find existing DIE or create new DIE 
/// for the given DITemplateTypeParameter.
DIE *
CompileUnit::getOrCreateTemplateTypeParameterDIE(DITemplateTypeParameter TP) {
  DIE *ParamDIE = getDIE(TP);
  if (ParamDIE)
    return ParamDIE;

  ParamDIE = new DIE(dwarf::DW_TAG_template_type_parameter);
  addType(ParamDIE, TP.getType());
  addString(ParamDIE, dwarf::DW_AT_name, dwarf::DW_FORM_string, TP.getName());
  return ParamDIE;
}

/// getOrCreateTemplateValueParameterDIE - Find existing DIE or create new DIE 
/// for the given DITemplateValueParameter.
DIE *
CompileUnit::getOrCreateTemplateValueParameterDIE(DITemplateValueParameter TPV) {
  DIE *ParamDIE = getDIE(TPV);
  if (ParamDIE)
    return ParamDIE;

  ParamDIE = new DIE(dwarf::DW_TAG_template_value_parameter);
  addType(ParamDIE, TPV.getType());
  if (!TPV.getName().empty())
    addString(ParamDIE, dwarf::DW_AT_name, dwarf::DW_FORM_string, TPV.getName());
  addUInt(ParamDIE, dwarf::DW_AT_const_value, dwarf::DW_FORM_udata, 
          TPV.getValue());
  return ParamDIE;
}

/// getOrCreateNameSpace - Create a DIE for DINameSpace.
DIE *CompileUnit::getOrCreateNameSpace(DINameSpace NS) {
  DIE *NDie = getDIE(NS);
  if (NDie)
    return NDie;
  NDie = new DIE(dwarf::DW_TAG_namespace);
  insertDIE(NS, NDie);
  if (!NS.getName().empty())
    addString(NDie, dwarf::DW_AT_name, dwarf::DW_FORM_string, NS.getName());
  addSourceLine(NDie, NS);
  addToContextOwner(NDie, NS.getContext());
  return NDie;
}

/// constructSubrangeDIE - Construct subrange DIE from DISubrange.
void CompileUnit::constructSubrangeDIE(DIE &Buffer, DISubrange SR, DIE *IndexTy){
  DIE *DW_Subrange = new DIE(dwarf::DW_TAG_subrange_type);
  addDIEEntry(DW_Subrange, dwarf::DW_AT_type, dwarf::DW_FORM_ref4, IndexTy);
  int64_t L = SR.getLo();
  int64_t H = SR.getHi();

  // The L value defines the lower bounds which is typically zero for C/C++. The
  // H value is the upper bounds.  Values are 64 bit.  H - L + 1 is the size
  // of the array. If L > H then do not emit DW_AT_lower_bound and 
  // DW_AT_upper_bound attributes. If L is zero and H is also zero then the
  // array has one element and in such case do not emit lower bound.

  if (L > H) {
    Buffer.addChild(DW_Subrange);
    return;
  }
  if (L)
    addSInt(DW_Subrange, dwarf::DW_AT_lower_bound, 0, L);
  addSInt(DW_Subrange, dwarf::DW_AT_upper_bound, 0, H);
  Buffer.addChild(DW_Subrange);
}

/// constructArrayTypeDIE - Construct array type DIE from DICompositeType.
void CompileUnit::constructArrayTypeDIE(DIE &Buffer,
                                        DICompositeType *CTy) {
  Buffer.setTag(dwarf::DW_TAG_array_type);
  if (CTy->getTag() == dwarf::DW_TAG_vector_type)
    addUInt(&Buffer, dwarf::DW_AT_GNU_vector, dwarf::DW_FORM_flag, 1);

  // Emit derived type.
  addType(&Buffer, CTy->getTypeDerivedFrom());
  DIArray Elements = CTy->getTypeArray();

  // Get an anonymous type for index type.
  DIE *IdxTy = getIndexTyDie();
  if (!IdxTy) {
    // Construct an anonymous type for index type.
    IdxTy = new DIE(dwarf::DW_TAG_base_type);
    addUInt(IdxTy, dwarf::DW_AT_byte_size, 0, sizeof(int32_t));
    addUInt(IdxTy, dwarf::DW_AT_encoding, dwarf::DW_FORM_data1,
            dwarf::DW_ATE_signed);
    addDie(IdxTy);
    setIndexTyDie(IdxTy);
  }

  // Add subranges to array type.
  for (unsigned i = 0, N = Elements.getNumElements(); i < N; ++i) {
    DIDescriptor Element = Elements.getElement(i);
    if (Element.getTag() == dwarf::DW_TAG_subrange_type)
      constructSubrangeDIE(Buffer, DISubrange(Element), IdxTy);
  }
}

/// constructEnumTypeDIE - Construct enum type DIE from DIEnumerator.
DIE *CompileUnit::constructEnumTypeDIE(DIEnumerator ETy) {
  DIE *Enumerator = new DIE(dwarf::DW_TAG_enumerator);
  StringRef Name = ETy.getName();
  addString(Enumerator, dwarf::DW_AT_name, dwarf::DW_FORM_string, Name);
  int64_t Value = ETy.getEnumValue();
  addSInt(Enumerator, dwarf::DW_AT_const_value, dwarf::DW_FORM_sdata, Value);
  return Enumerator;
}

/// createMemberDIE - Create new member DIE.
DIE *CompileUnit::createMemberDIE(DIDerivedType DT) {
  DIE *MemberDie = new DIE(DT.getTag());
  StringRef Name = DT.getName();
  if (!Name.empty())
    addString(MemberDie, dwarf::DW_AT_name, dwarf::DW_FORM_string, Name);

  addType(MemberDie, DT.getTypeDerivedFrom());

  addSourceLine(MemberDie, DT);

  DIEBlock *MemLocationDie = new (DIEValueAllocator) DIEBlock();
  addUInt(MemLocationDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_plus_uconst);

  uint64_t Size = DT.getSizeInBits();
  uint64_t FieldSize = DT.getOriginalTypeSize();

  if (Size != FieldSize) {
    // Handle bitfield.
    addUInt(MemberDie, dwarf::DW_AT_byte_size, 0, DT.getOriginalTypeSize()>>3);
    addUInt(MemberDie, dwarf::DW_AT_bit_size, 0, DT.getSizeInBits());

    uint64_t Offset = DT.getOffsetInBits();
    uint64_t AlignMask = ~(DT.getAlignInBits() - 1);
    uint64_t HiMark = (Offset + FieldSize) & AlignMask;
    uint64_t FieldOffset = (HiMark - FieldSize);
    Offset -= FieldOffset;

    // Maybe we need to work from the other end.
    if (Asm->getTargetData().isLittleEndian())
      Offset = FieldSize - (Offset + Size);
    addUInt(MemberDie, dwarf::DW_AT_bit_offset, 0, Offset);

    // Here WD_AT_data_member_location points to the anonymous
    // field that includes this bit field.
    addUInt(MemLocationDie, 0, dwarf::DW_FORM_udata, FieldOffset >> 3);

  } else
    // This is not a bitfield.
    addUInt(MemLocationDie, 0, dwarf::DW_FORM_udata, DT.getOffsetInBits() >> 3);

  if (DT.getTag() == dwarf::DW_TAG_inheritance
      && DT.isVirtual()) {

    // For C++, virtual base classes are not at fixed offset. Use following
    // expression to extract appropriate offset from vtable.
    // BaseAddr = ObAddr + *((*ObAddr) - Offset)

    DIEBlock *VBaseLocationDie = new (DIEValueAllocator) DIEBlock();
    addUInt(VBaseLocationDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_dup);
    addUInt(VBaseLocationDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_deref);
    addUInt(VBaseLocationDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_constu);
    addUInt(VBaseLocationDie, 0, dwarf::DW_FORM_udata, DT.getOffsetInBits());
    addUInt(VBaseLocationDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_minus);
    addUInt(VBaseLocationDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_deref);
    addUInt(VBaseLocationDie, 0, dwarf::DW_FORM_data1, dwarf::DW_OP_plus);

    addBlock(MemberDie, dwarf::DW_AT_data_member_location, 0,
             VBaseLocationDie);
  } else
    addBlock(MemberDie, dwarf::DW_AT_data_member_location, 0, MemLocationDie);

  if (DT.isProtected())
    addUInt(MemberDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_flag,
            dwarf::DW_ACCESS_protected);
  else if (DT.isPrivate())
    addUInt(MemberDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_flag,
            dwarf::DW_ACCESS_private);
  // Otherwise C++ member and base classes are considered public.
  else if (DT.getCompileUnit().getLanguage() == dwarf::DW_LANG_C_plus_plus)
    addUInt(MemberDie, dwarf::DW_AT_accessibility, dwarf::DW_FORM_flag,
            dwarf::DW_ACCESS_public);
  if (DT.isVirtual())
    addUInt(MemberDie, dwarf::DW_AT_virtuality, dwarf::DW_FORM_flag,
            dwarf::DW_VIRTUALITY_virtual);

  // Objective-C properties.
  StringRef PropertyName = DT.getObjCPropertyName();
  if (!PropertyName.empty()) {
    addString(MemberDie, dwarf::DW_AT_APPLE_property_name, dwarf::DW_FORM_string,
              PropertyName);
    StringRef GetterName = DT.getObjCPropertyGetterName();
    if (!GetterName.empty())
      addString(MemberDie, dwarf::DW_AT_APPLE_property_getter,
                dwarf::DW_FORM_string, GetterName);
    StringRef SetterName = DT.getObjCPropertySetterName();
    if (!SetterName.empty())
      addString(MemberDie, dwarf::DW_AT_APPLE_property_setter,
                dwarf::DW_FORM_string, SetterName);
    unsigned PropertyAttributes = 0;
    if (DT.isReadOnlyObjCProperty())
      PropertyAttributes |= dwarf::DW_APPLE_PROPERTY_readonly;
    if (DT.isReadWriteObjCProperty())
      PropertyAttributes |= dwarf::DW_APPLE_PROPERTY_readwrite;
    if (DT.isAssignObjCProperty())
      PropertyAttributes |= dwarf::DW_APPLE_PROPERTY_assign;
    if (DT.isRetainObjCProperty())
      PropertyAttributes |= dwarf::DW_APPLE_PROPERTY_retain;
    if (DT.isCopyObjCProperty())
      PropertyAttributes |= dwarf::DW_APPLE_PROPERTY_copy;
    if (DT.isNonAtomicObjCProperty())
      PropertyAttributes |= dwarf::DW_APPLE_PROPERTY_nonatomic;
    if (PropertyAttributes)
      addUInt(MemberDie, dwarf::DW_AT_APPLE_property_attribute, 0, 
              PropertyAttributes);
  }
  return MemberDie;
}
