#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "llvm/BinaryFormat/ELF.h"

namespace dast {

// Describes a constant unnamed_addr data object used by a stencil, usually a string literal, constant array, etc.
// Such data objects are dumped directly into the generated C++ file.
//
struct StencilSharedConstantDataObject
{
    struct Element
    {
        enum Kind
        {
            // A single byte
            //
            ByteConstant,
            // A 8-byte pointer referencing another StencilSharedConstantDataObject plus an addend
            // TODO: we should also handle the case where the pointer refers to a C symbol, but currently we don't have such use case yet.
            //
            PointerWithAddend
        };

        Kind m_kind;
        // The byte value if m_kind == ByteConstant
        //
        uint8_t m_byteValue;
        // The pointer value and SectionRef if m_kind == PointerWithAddend
        //
        StencilSharedConstantDataObject* m_ptrValue;
        llvm::object::SectionRef m_sectionRef;
        // The addend if m_kind == PointerWithAddend
        //
        int64_t m_addend;
    };

    StencilSharedConstantDataObject()
        : m_uniqueLabel(static_cast<size_t>(-1))
        , m_alignment(static_cast<size_t>(-1))
        , m_shouldForwardDeclare(false)
    { }

    size_t WARN_UNUSED GetAlignment() { ReleaseAssert(m_alignment != static_cast<size_t>(-1) && m_alignment > 0); return m_alignment; }
    size_t WARN_UNUSED GetUniqueLabel() { ReleaseAssert(m_uniqueLabel != static_cast<size_t>(-1)); return m_uniqueLabel; }

    size_t ComputeTrueSizeWithoutPadding()
    {
        size_t res = 0;
        for (Element& e : m_valueDefs)
        {
            switch (e.m_kind)
            {
            case Element::ByteConstant: { res += 1; break; }
            case Element::PointerWithAddend: { res += 8; break; }
            }
        }
        return res;
    }

    size_t ComputeSizeWithPadding()
    {
        size_t res = ComputeTrueSizeWithoutPadding();
        res = (res + GetAlignment() - 1) / GetAlignment() * GetAlignment();
        return res;
    }

    size_t ComputeNumPaddingBytes() { return ComputeSizeWithPadding() - ComputeTrueSizeWithoutPadding(); }

    // Print C++ declaration part
    //
    std::string WARN_UNUSED PrintDeclaration();

    // Print C++ definition part
    //
    std::string WARN_UNUSED PrintDefinition();

    // A unique label assigned to this object for printing C++ code
    //
    size_t m_uniqueLabel;
    // The alignment of this object
    //
    size_t m_alignment;
    // True if this object is referenced by other objects by pointer, so a forward declaration is needed
    //
    bool m_shouldForwardDeclare;
    // The value definition
    //
    std::vector<Element> m_valueDefs;
};

struct RelocationRecord
{
    enum class SymKind
    {
        // The start address of the fast path logic for this bytecode
        //
        FastPathAddr,
        // The start address of the slow path logic for this bytecode
        //
        SlowPathAddr,
        // The start address of the current piece of IC logic
        // Only possible to show up if this is an object file for IC extraction
        //
        ICPathAddr,
        // Only possible to show up if this is an object file for IC extraction
        // For IC logic, 'PrivateDataAddr' refers to its own private data section.
        // However, it may also need to know the main logic's private data section.
        // This SymKind represents this case.
        //
        MainLogicPrivateDataAddr,
        // The start address of the private data object for this stencil
        //
        PrivateDataAddr,
        // A shared constant data object (StencilSharedConstantDataObject)
        //
        SharedConstantDataObject,
        // An external C symbol (e.g., the C++ slow path function)
        //
        ExternalCSymbol,
        // A copy-and-patch stencil hole
        //
        StencilHole
    };

    RelocationRecord()
        : m_relocationType(static_cast<size_t>(-1))
        , m_symKind(SymKind::ExternalCSymbol)
        , m_offset(static_cast<size_t>(-1))
        , m_sharedDataObject(nullptr)
        , m_symbolName("")
        , m_stencilHoleOrd(static_cast<size_t>(-1))
        , m_addend(0)
        , m_sectionRef()
    { }

    // One of the following: R_X86_64_PLT32, R_X86_64_PC32, R_X86_64_64, R_X86_64_32S, R_X86_64_32
    //
    uint64_t m_relocationType;
    SymKind m_symKind;
    // The offset of this relocation
    //
    size_t m_offset;
    // Only valid if m_symKind == SharedConstantDataObject
    //
    StencilSharedConstantDataObject* m_sharedDataObject;
    // Only valid if m_symKind == ExternalCSymbol
    //
    std::string m_symbolName;
    // Only valid if m_symKind == StencilHole
    //
    size_t m_stencilHoleOrd;
    // The addend for this relocation
    //
    int64_t m_addend;
    // Only valid if m_symKind == SharedConstantDataObject or PrivateDataAddr, internal use only
    //
    llvm::object::SectionRef m_sectionRef;
};

// Each stencil may have a private (i.e., per-stencil-instantiation) data section, storing e.g., jump tables,
// which needs to be instantiated whenever the stencil is instantiated.
// They cannot be made shared because they contain (or transitively contains) code section relocations.
// This class describes the layout of this object.
//
struct StencilPrivateDataObject
{
    size_t m_alignment;
    std::vector<uint8_t> m_bytes;
    std::vector<RelocationRecord> m_relocations;
};

class CPRuntimeConstantNodeBase;
class BytecodeVariantDefinition;

struct DeegenStencilCodegenResult
{
    struct CondBrLatePatchRecord
    {
        size_t m_offset;
        bool m_is64Bit;
    };

    std::string m_cppCode;

    std::vector<uint8_t> m_fastPathPreFixupCode;
    std::vector<uint8_t> m_slowPathPreFixupCode;
    std::vector<uint8_t> m_icPathPreFixupCode;
    std::vector<uint8_t> m_dataSecPreFixupCode;
    size_t m_dataSecAlignment;

    std::vector<CondBrLatePatchRecord> m_condBrFixupOffsetsInFastPath;
    std::vector<CondBrLatePatchRecord> m_condBrFixupOffsetsInSlowPath;
    std::vector<CondBrLatePatchRecord> m_condBrFixupOffsetsInDataSec;

    std::vector<bool> m_fastPathRelocMarker;
    std::vector<bool> m_slowPathRelocMarker;
    std::vector<bool> m_icPathRelocMarker;
    std::vector<bool> m_dataSecRelocMarker;

    bool m_isForIcLogicExtraction;

    static constexpr const char* x_fastPathCodegenFuncName = "deegen_do_codegen_fastpath";
    static constexpr const char* x_slowPathCodegenFuncName = "deegen_do_codegen_slowpath";
    static constexpr const char* x_icPathCodegenFuncName = "deegen_do_codegen_icpath";
    static constexpr const char* x_dataSecCodegenFuncName = "deegen_do_codegen_datasec";

    // Return a LLVM module that contains actually linkable codegen logic
    // 'originModule' should be the original module where the stencil object file is compiled from
    //
    std::unique_ptr<llvm::Module> WARN_UNUSED GenerateCodegenLogicLLVMModule(llvm::Module* originModule, const std::string& cppStorePath = "");

    // Create LLVM logic that decodes the SlowPathData and return the bytecode operand vector expected by the codegen function
    // The vector may contain nullptr, in which case caller should assert that the argument is indeed unused by the codegen function, and pass undef instead
    //
    static std::vector<llvm::Value*> WARN_UNUSED BuildBytecodeOperandVectorFromSlowPathData(BytecodeVariantDefinition* bytecodeDef,
                                                                                            llvm::Value* slowPathData,
                                                                                            llvm::Value* slowPathDataOffset,
                                                                                            llvm::Value* codeBlock32,
                                                                                            llvm::BasicBlock* insertAtEnd);
};

struct DeegenStencil
{
    // Note:
    // For isExtractIcLogic == true:
    //     m_icPathCode / m_icPathRelos will be populated to store the result
    //     m_fastPathCode / m_slowPathCode will be populated for assertion purpose, but m_fastPathRelos / m_slowPathRelos are not populated
    //
    // For isExtractIcLogic == false:
    //     m_fastPathCode / m_fastPathRelos / m_slowPathCode / m_slowPathRelos will be populated to store the result
    //
    std::vector<StencilSharedConstantDataObject*> m_sharedDataObjs;
    std::vector<uint8_t> m_fastPathCode;
    std::vector<RelocationRecord> m_fastPathRelos;
    std::vector<uint8_t> m_slowPathCode;
    std::vector<RelocationRecord> m_slowPathRelos;
    std::vector<uint8_t> m_icPathCode;
    std::vector<RelocationRecord> m_icPathRelos;
    StencilPrivateDataObject m_privateDataObject;
    using SectionToPdoOffsetMapTy = std::unordered_map<std::string /*secName*/, uint64_t /*offset*/>;
    SectionToPdoOffsetMapTy m_sectionToPdoOffsetMap;
    std::unordered_map<std::string, uint64_t> m_labelDistanceComputations;
    llvm::Triple m_triple;
    bool m_isForIcLogicExtraction;

    // Prints a C++ file that defines 3 functions 'deegen_do_codegen_[fastpath/slowpath/datasec]' with the following parameters
    //     uint8_t* destAddr,
    //     uint64_t fastPathAddr,
    //     uint64_t slowPathAddr,
    //     uint64_t dataSecAddr,
    //     N * int64_t bytecodeValXX
    //
    // Note that the functions only contain the patch logic, not the copy logic. This is because we may need to
    // merge multiple stencils into one, and it turns out that LLVM optimizer is not smart enough to merge multiple
    // memcpy together, so we instead do it by hand afterwards.
    //
    // 'extraPlaceholderOrds' may be optionally provided if the stencil uses manually-reserved special placeholder ordinals
    // All of those ordinals must be >= 10000 to avoid interfering with normal placeholders.
    //
    DeegenStencilCodegenResult WARN_UNUSED PrintCodegenFunctions(
        bool mayAttemptToEliminateJmpToFallthrough,
        size_t numBytecodeOperands,
        size_t numGenericIcTotalCaptures,
        const std::vector<CPRuntimeConstantNodeBase*>& placeholders,
        const std::vector<size_t>& extraPlaceholderOrds = {});

    static DeegenStencil WARN_UNUSED ParseMainLogic(llvm::LLVMContext& ctx, const std::string& objFile)
    {
        return ParseImpl(ctx, objFile, false /*isExtractIcLogic*/, {} /*mainLogicPdoLayout*/);
    }

    static DeegenStencil WARN_UNUSED ParseIcLogic(llvm::LLVMContext& ctx, const std::string& objFile, const SectionToPdoOffsetMapTy& mainLogicPdoOffsetMap)
    {
        return ParseImpl(ctx, objFile, true /*isExtractIcLogic*/, mainLogicPdoOffsetMap);
    }

    uint64_t WARN_UNUSED RetrieveLabelDistanceComputationResult(const std::string& varName) const
    {
        ReleaseAssert(m_labelDistanceComputations.count(varName));
        return m_labelDistanceComputations.find(varName)->second;
    }

private:
    static DeegenStencil WARN_UNUSED ParseImpl(llvm::LLVMContext& ctx,
                                               const std::string& objFile,
                                               bool isExtractIcLogic,
                                               SectionToPdoOffsetMapTy mainLogicPdoLayout);
};

// Dump stencil machine code to human-readable diassembly for audit purpose
// Note that relocation bytes are only marked with '**' and not fixed up for simplicity
//
std::string WARN_UNUSED DumpStencilDisassemblyForAuditPurpose(
    llvm::Triple triple,
    bool isDataSection,
    const std::vector<uint8_t>& preFixupCode,
    const std::vector<bool>& isPartOfReloc,
    const std::string& linePrefix);

}   // namespace dast
