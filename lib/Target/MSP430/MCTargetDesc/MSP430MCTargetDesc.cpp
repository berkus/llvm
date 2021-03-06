//===-- MSP430MCTargetDesc.cpp - MSP430 Target Descriptions ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides MSP430 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "MSP430MCTargetDesc.h"
#include "InstPrinter/MSP430InstPrinter.h"
#include "MSP430MCAsmInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "MSP430GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "MSP430GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "MSP430GenRegisterInfo.inc"

static MCInstrInfo *createMSP430MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitMSP430MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createMSP430MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitMSP430MCRegisterInfo(X, MSP430::PC);
  return X;
}

static MCSubtargetInfo *
createMSP430MCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  return createMSP430MCSubtargetInfoImpl(TT, CPU, FS);
}

static MCInstPrinter *createMSP430MCInstPrinter(const Triple &T,
                                                unsigned SyntaxVariant,
                                                const MCAsmInfo &MAI,
                                                const MCInstrInfo &MII,
                                                const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new MSP430InstPrinter(MAI, MII, MRI);
  return nullptr;
}

static MCCodeEmitter *createMSP430MCCodeEmitter(const MCInstrInfo &II,
                                                const MCRegisterInfo &MRI,
                                                MCContext &Ctx)
{
    return nullptr;
}

static MCStreamer *createMCStreamer(const Triple &T, MCContext &Context,
                                    MCAsmBackend &MAB, raw_pwrite_stream &OS,
                                    MCCodeEmitter *Emitter, bool RelaxAll) {
  return createELFStreamer(Context, MAB, OS, Emitter, RelaxAll);
}

static MCTargetStreamer *createMSP430ObjectTargetStreamer(
        MCStreamer &S, const MCSubtargetInfo &STI)
{
    return nullptr;
}

static MCTargetStreamer *createMCAsmTargetStreamer(
        MCStreamer &S, formatted_raw_ostream &OS, MCInstPrinter *InstPrint,
        bool IsVerboseAsm)
{
    return nullptr;
}

static MCAsmBackend *createMSP430AsmBackend_(const Target & /*T*/,
                                      const MCRegisterInfo & /*MRI*/,
                                      const Triple &TT, StringRef /*CPU*/,
                                      const MCTargetOptions & /*Options*/) {
  return createMSP430AsmBackend(TT);
}


extern "C" void LLVMInitializeMSP430TargetMC() {
  // Register the MC asm info.
  RegisterMCAsmInfo<MSP430MCAsmInfo> X(getTheMSP430Target());

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(getTheMSP430Target(),
                                      createMSP430MCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(getTheMSP430Target(),
                                    createMSP430MCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(getTheMSP430Target(),
                                          createMSP430MCSubtargetInfo);

  // Register the MCInstPrinter.
  TargetRegistry::RegisterMCInstPrinter(getTheMSP430Target(),
                                        createMSP430MCInstPrinter);

  // Register the MC Code Emitter
  TargetRegistry::RegisterMCCodeEmitter(getTheMSP430Target(),
                                        createMSP430MCCodeEmitter);

  // Register the ELF streamer
  TargetRegistry::RegisterELFStreamer(getTheMSP430Target(),
                                      createMCStreamer);

  // Register the obj target streamer.
  TargetRegistry::RegisterObjectTargetStreamer(getTheMSP430Target(),
                                               createMSP430ObjectTargetStreamer);

  // Register the asm target streamer.
  TargetRegistry::RegisterAsmTargetStreamer(getTheMSP430Target(),
                                            createMCAsmTargetStreamer);

  // Register the asm backend (as little endian).
  TargetRegistry::RegisterMCAsmBackend(getTheMSP430Target(),
                                       createMSP430AsmBackend_);
}
