#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCELFObjectWriter.h"

//#include "MSP430AsmBackend.h"

using namespace llvm;

namespace {

class MSP430AsmBackend final : public MCAsmBackend {

public:
  explicit MSP430AsmBackend() : MCAsmBackend() {}
  ~MSP430AsmBackend() override {}

  void applyFixup(const MCFixup &Fixup, char *Data, unsigned DataSize,
                  uint64_t Value, bool IsPCRel) const override;

  MCObjectWriter *createObjectWriter(raw_pwrite_stream &OS) const override;

  // No instruction requires relaxation
  bool fixupNeedsRelaxation(const MCFixup &Fixup, uint64_t Value,
                            const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const override {
    return false;
  }

  unsigned getNumFixupKinds() const override {
    // We currently just use the generic fixups in MCFixup.h and don't have any
    // target-specific fixups.
    return 0;
  }

  bool mayNeedRelaxation(const MCInst &Inst) const override { return false; }

  void relaxInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                        MCInst &Res) const override {}

  bool writeNopData(uint64_t Count, MCObjectWriter *OW) const override;
};

bool MSP430AsmBackend::writeNopData(uint64_t Count, MCObjectWriter *OW) const {
  // for (uint64_t i = 0; i < Count; ++i)
    // OW->write8(WebAssembly::Nop);// fixme

  return true;
}

void MSP430AsmBackend::applyFixup(const MCFixup &Fixup, char *Data,
                                          unsigned DataSize, uint64_t Value,
                                          bool IsPCRel) const {
  const MCFixupKindInfo &Info = getFixupKindInfo(Fixup.getKind());
  assert(Info.Flags == 0 && "MSP430 does not use MCFixupKindInfo flags");

  unsigned NumBytes = alignTo(Info.TargetSize, 8) / 8;
  if (Value == 0)
    return; // Doesn't change encoding.

  // Shift the value into position.
  Value <<= Info.TargetOffset;

  unsigned Offset = Fixup.getOffset();
  assert(Offset + NumBytes <= DataSize && "Invalid fixup offset!");

  // For each byte of the fragment that the fixup touches, mask in the
  // bits from the fixup value.
  for (unsigned i = 0; i != NumBytes; ++i)
    Data[Offset + i] |= uint8_t((Value >> (i * 8)) & 0xff);
}

MCObjectWriter *
MSP430AsmBackend::createObjectWriter(raw_pwrite_stream &OS) const {
  return createMSP430ELFObjectWriter(OS);
}
} // end anonymous namespace

MCAsmBackend *llvm::createMSP430AsmBackend(const Triple &) {
  return new MSP430AsmBackend();
}
