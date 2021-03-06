//===-- TypeStreamMerger.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/TypeStreamMerger.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbackPipeline.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace llvm;
using namespace llvm::codeview;

namespace {

/// Implementation of CodeView type stream merging.
///
/// A CodeView type stream is a series of records that reference each other
/// through type indices. A type index is either "simple", meaning it is less
/// than 0x1000 and refers to a builtin type, or it is complex, meaning it
/// refers to a prior type record in the current stream. The type index of a
/// record is equal to the number of records before it in the stream plus
/// 0x1000.
///
/// Type records are only allowed to use type indices smaller than their own, so
/// a type stream is effectively a topologically sorted DAG. Cycles occuring in
/// the type graph of the source program are resolved with forward declarations
/// of composite types. This class implements the following type stream merging
/// algorithm, which relies on this DAG structure:
///
/// - Begin with a new empty stream, and a new empty hash table that maps from
///   type record contents to new type index.
/// - For each new type stream, maintain a map from source type index to
///   destination type index.
/// - For each record, copy it and rewrite its type indices to be valid in the
///   destination type stream.
/// - If the new type record is not already present in the destination stream
///   hash table, append it to the destination type stream, assign it the next
///   type index, and update the two hash tables.
/// - If the type record already exists in the destination stream, discard it
///   and update the type index map to forward the source type index to the
///   existing destination type index.
class TypeStreamMerger : public TypeVisitorCallbacks {
public:
  TypeStreamMerger(TypeTableBuilder &DestStream, TypeServerHandler *Handler)
      : DestStream(DestStream), FieldListBuilder(DestStream), Handler(Handler) {
  }

/// TypeVisitorCallbacks overrides.
#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  Error visitKnownRecord(CVType &CVR, Name##Record &Record) override;
#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownMember(CVMemberRecord &CVR, Name##Record &Record) override;
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/TypeRecords.def"

  Error visitUnknownType(CVType &Record) override;

  Error visitTypeBegin(CVType &Record) override;
  Error visitTypeEnd(CVType &Record) override;
  Error visitMemberEnd(CVMemberRecord &Record) override;

  Error mergeStream(const CVTypeArray &Types);

private:
  bool remapIndex(TypeIndex &Idx);

  template <typename RecordType>
  Error writeRecord(RecordType &R, bool RemapSuccess) {
    if (!RemapSuccess) {
      LastError = joinErrors(
          std::move(*LastError),
          llvm::make_error<CodeViewError>(cv_error_code::corrupt_record));
    }
    IndexMap.push_back(DestStream.writeKnownType(R));
    return Error::success();
  }

  template <typename RecordType>
  Error writeMember(RecordType &R, bool RemapSuccess) {
    if (!RemapSuccess) {
      LastError = joinErrors(
          std::move(*LastError),
          llvm::make_error<CodeViewError>(cv_error_code::corrupt_record));
    }
    FieldListBuilder.writeMemberType(R);
    return Error::success();
  }

  Optional<Error> LastError;

  BumpPtrAllocator Allocator;

  TypeTableBuilder &DestStream;
  FieldListRecordBuilder FieldListBuilder;
  TypeServerHandler *Handler;

  bool IsInFieldList = false;
  size_t BeginIndexMapSize = 0;

  /// Map from source type index to destination type index. Indexed by source
  /// type index minus 0x1000.
  SmallVector<TypeIndex, 0> IndexMap;
};

} // end anonymous namespace

Error TypeStreamMerger::visitTypeBegin(CVRecord<TypeLeafKind> &Rec) {
  if (Rec.Type == TypeLeafKind::LF_FIELDLIST) {
    assert(!IsInFieldList);
    IsInFieldList = true;
    FieldListBuilder.begin();
  } else
    BeginIndexMapSize = IndexMap.size();
  return Error::success();
}

Error TypeStreamMerger::visitTypeEnd(CVRecord<TypeLeafKind> &Rec) {
  if (Rec.Type == TypeLeafKind::LF_FIELDLIST) {
    TypeIndex Index = FieldListBuilder.end();
    IndexMap.push_back(Index);
    IsInFieldList = false;
  }
  return Error::success();
}

Error TypeStreamMerger::visitMemberEnd(CVMemberRecord &Rec) {
  assert(IndexMap.size() == BeginIndexMapSize + 1);
  return Error::success();
}

bool TypeStreamMerger::remapIndex(TypeIndex &Idx) {
  // Simple types are unchanged.
  if (Idx.isSimple())
    return true;
  unsigned MapPos = Idx.getIndex() - TypeIndex::FirstNonSimpleIndex;
  if (MapPos < IndexMap.size()) {
    Idx = IndexMap[MapPos];
    return true;
  }

  // This type index is invalid. Remap this to "not translated by cvpack",
  // and return failure.
  Idx = TypeIndex(SimpleTypeKind::NotTranslated, SimpleTypeMode::Direct);
  return false;
}

Error TypeStreamMerger::visitKnownRecord(CVType &, ModifierRecord &R) {
  return writeRecord(R, remapIndex(R.ModifiedType));
}

Error TypeStreamMerger::visitKnownRecord(CVType &, ProcedureRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.ReturnType);
  Success &= remapIndex(R.ArgumentList);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, MemberFunctionRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.ReturnType);
  Success &= remapIndex(R.ClassType);
  Success &= remapIndex(R.ThisType);
  Success &= remapIndex(R.ArgumentList);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, MemberFuncIdRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.ClassType);
  Success &= remapIndex(R.FunctionType);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, ArgListRecord &R) {
  bool Success = true;
  for (TypeIndex &Arg : R.ArgIndices)
    Success &= remapIndex(Arg);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, StringListRecord &R) {
  bool Success = true;
  for (TypeIndex &Str : R.StringIndices)
    Success &= remapIndex(Str);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, PointerRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.ReferentType);
  if (R.isPointerToMember())
    Success &= remapIndex(R.MemberInfo->ContainingType);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, ArrayRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.ElementType);
  Success &= remapIndex(R.IndexType);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, ClassRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.FieldList);
  Success &= remapIndex(R.DerivationList);
  Success &= remapIndex(R.VTableShape);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, UnionRecord &R) {
  return writeRecord(R, remapIndex(R.FieldList));
}

Error TypeStreamMerger::visitKnownRecord(CVType &, EnumRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.FieldList);
  Success &= remapIndex(R.UnderlyingType);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, BitFieldRecord &R) {
  return writeRecord(R, remapIndex(R.Type));
}

Error TypeStreamMerger::visitKnownRecord(CVType &, VFTableShapeRecord &R) {
  return writeRecord(R, true);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, TypeServer2Record &R) {
  return writeRecord(R, true);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, StringIdRecord &R) {
  return writeRecord(R, remapIndex(R.Id));
}

Error TypeStreamMerger::visitKnownRecord(CVType &, FuncIdRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.ParentScope);
  Success &= remapIndex(R.FunctionType);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, UdtSourceLineRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.UDT);
  Success &= remapIndex(R.SourceFile);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, UdtModSourceLineRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.UDT);
  Success &= remapIndex(R.SourceFile);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, BuildInfoRecord &R) {
  bool Success = true;
  for (TypeIndex &Arg : R.ArgIndices)
    Success &= remapIndex(Arg);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, VFTableRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.CompleteClass);
  Success &= remapIndex(R.OverriddenVFTable);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &,
                                         MethodOverloadListRecord &R) {
  bool Success = true;
  for (OneMethodRecord &Meth : R.Methods)
    Success &= remapIndex(Meth.Type);
  return writeRecord(R, Success);
}

Error TypeStreamMerger::visitKnownRecord(CVType &, FieldListRecord &R) {
  // Visit the members inside the field list.
  CVTypeVisitor Visitor(*this);
  if (auto EC = Visitor.visitFieldListMemberStream(R.Data))
    return EC;
  return Error::success();
}

Error TypeStreamMerger::visitKnownMember(CVMemberRecord &,
                                         NestedTypeRecord &R) {
  return writeMember(R, remapIndex(R.Type));
}

Error TypeStreamMerger::visitKnownMember(CVMemberRecord &, OneMethodRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.Type);
  return writeMember(R, Success);
}

Error TypeStreamMerger::visitKnownMember(CVMemberRecord &,
                                         OverloadedMethodRecord &R) {
  return writeMember(R, remapIndex(R.MethodList));
}

Error TypeStreamMerger::visitKnownMember(CVMemberRecord &,
                                         DataMemberRecord &R) {
  return writeMember(R, remapIndex(R.Type));
}

Error TypeStreamMerger::visitKnownMember(CVMemberRecord &,
                                         StaticDataMemberRecord &R) {
  return writeMember(R, remapIndex(R.Type));
}

Error TypeStreamMerger::visitKnownMember(CVMemberRecord &,
                                         EnumeratorRecord &R) {
  return writeMember(R, true);
}

Error TypeStreamMerger::visitKnownMember(CVMemberRecord &, VFPtrRecord &R) {
  return writeMember(R, remapIndex(R.Type));
}

Error TypeStreamMerger::visitKnownMember(CVMemberRecord &, BaseClassRecord &R) {
  return writeMember(R, remapIndex(R.Type));
}

Error TypeStreamMerger::visitKnownMember(CVMemberRecord &,
                                         VirtualBaseClassRecord &R) {
  bool Success = true;
  Success &= remapIndex(R.BaseType);
  Success &= remapIndex(R.VBPtrType);
  return writeMember(R, Success);
}

Error TypeStreamMerger::visitKnownMember(CVMemberRecord &,
                                         ListContinuationRecord &R) {
  return writeMember(R, remapIndex(R.ContinuationIndex));
}

Error TypeStreamMerger::visitUnknownType(CVType &Rec) {
  // We failed to translate a type. Translate this index as "not translated".
  IndexMap.push_back(
      TypeIndex(SimpleTypeKind::NotTranslated, SimpleTypeMode::Direct));
  return llvm::make_error<CodeViewError>(cv_error_code::corrupt_record);
}

Error TypeStreamMerger::mergeStream(const CVTypeArray &Types) {
  assert(IndexMap.empty());
  TypeVisitorCallbackPipeline Pipeline;
  LastError = Error::success();

  TypeDeserializer Deserializer;
  Pipeline.addCallbackToPipeline(Deserializer);
  Pipeline.addCallbackToPipeline(*this);

  CVTypeVisitor Visitor(Pipeline);
  if (Handler)
    Visitor.addTypeServerHandler(*Handler);

  if (auto EC = Visitor.visitTypeStream(Types))
    return EC;
  IndexMap.clear();

  Error Ret = std::move(*LastError);
  LastError.reset();
  return Ret;
}

Error llvm::codeview::mergeTypeStreams(TypeTableBuilder &DestStream,
                                       TypeServerHandler *Handler,
                                       const CVTypeArray &Types) {
  return TypeStreamMerger(DestStream, Handler).mergeStream(Types);
}
