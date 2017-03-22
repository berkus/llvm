#include "MCTargetDesc/MSP430MCTargetDesc.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;

namespace {

class MSP430ELFObjectWriter final : public MCELFObjectTargetWriter {
public:
  MSP430ELFObjectWriter();

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};
} // end anonymous namespace

MSP430ELFObjectWriter::MSP430ELFObjectWriter()
    : MCELFObjectTargetWriter(/*Is64Bit=*/false, /*OSABI=*/0, ELF::EM_WEBASSEMBLY,// need MSP elf constant
                              /*HasRelocationAddend=*/false) {}

unsigned MSP430ELFObjectWriter::getRelocType(MCContext &Ctx,
                                                  const MCValue &Target,
                                                  const MCFixup &Fixup,
                                                  bool IsPCRel) const {
  // MSP430 functions are not allocated in the address space. To resolve a
  // pointer to a function, we must use a special relocation type.
  // if (const MCSymbolRefExpr *SyExp =
  //         dyn_cast<MCSymbolRefExpr>(Fixup.getValue()))
  //   if (SyExp->getKind() == MCSymbolRefExpr::VK_MSP430_FUNCTION)
  //     return ELF::R_WEBASSEMBLY_FUNCTION;

  switch (Fixup.getKind()) {
  case FK_Data_4:
    assert(!is64Bit() && "4-byte relocations only supported on wasm32");
    return ELF::R_WEBASSEMBLY_DATA;
  case FK_Data_8:
    assert(is64Bit() && "8-byte relocations only supported on wasm64");
    return ELF::R_WEBASSEMBLY_DATA;
  default:
    llvm_unreachable("unimplemented fixup kind");
  }
}

MCObjectWriter *llvm::createMSP430ELFObjectWriter(raw_pwrite_stream &OS) {
  MCELFObjectTargetWriter *MOTW = new MSP430ELFObjectWriter();
  return createELFObjectWriter(MOTW, OS, /*IsLittleEndian=*/true);
}
