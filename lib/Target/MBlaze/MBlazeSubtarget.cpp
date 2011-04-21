//===- MBlazeSubtarget.cpp - MBlaze Subtarget Information -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the MBlaze specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#include "MBlazeSubtarget.h"
#include "MBlaze.h"
#include "MBlazeRegisterInfo.h"
#include "MBlazeGenSubtarget.inc"
#include "llvm/Support/CommandLine.h"
using namespace llvm;

MBlazeSubtarget::MBlazeSubtarget(const std::string &TT, const std::string &FS):
  HasBarrel(false), HasDiv(false), HasMul(false), HasPatCmp(false),
  HasFPU(false), HasMul64(false), HasSqrt(false)
{
  // Parse features string.
  std::string CPU = "mblaze";
  CPU = ParseSubtargetFeatures(FS, CPU);

  // Only use instruction scheduling if the selected CPU has an instruction
  // itinerary (the default CPU is the only one that doesn't).
  HasItin = CPU != "mblaze";
  DEBUG(dbgs() << "CPU " << CPU << "(" << HasItin << ")\n");

  // Compute the issue width of the MBlaze itineraries
  computeIssueWidth();
}

void MBlazeSubtarget::computeIssueWidth() {
  InstrItins.IssueWidth = 1;
}

bool MBlazeSubtarget::
enablePostRAScheduler(CodeGenOpt::Level OptLevel,
                      TargetSubtarget::AntiDepBreakMode& Mode,
                      RegClassVector& CriticalPathRCs) const {
  Mode = TargetSubtarget::ANTIDEP_CRITICAL;
  CriticalPathRCs.clear();
  CriticalPathRCs.push_back(&MBlaze::GPRRegClass);
  return HasItin && OptLevel >= CodeGenOpt::Default;
}

