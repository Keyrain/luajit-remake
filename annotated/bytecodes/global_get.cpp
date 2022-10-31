#include "api_define_bytecode.h"
#include "deegen_api.h"
#include "api_inline_cache.h"

#include "runtime_utils.h"

static void NO_RETURN GlobalGetMetamethodCallContinuation(TValue /*tvIndex*/)
{
    Return(GetReturnValue(0));
}

static void NO_RETURN GlobalGetImpl(TValue tvIndex)
{
    assert(tvIndex.Is<tString>());
    HeapPtr<HeapString> index = tvIndex.As<tString>();
    HeapPtr<TableObject> base = GetFEnvGlobalObject();

    {
        ICHandler* ic = MakeInlineCache();
        ic->AddKey(base->m_hiddenClass.m_value).SpecifyImpossibleValue(0);
        auto [result, mayHaveMt] = ic->Body([ic, base, index]() -> std::pair<TValue, bool> {
            GetByIdICInfo c_info;
            TableObject::PrepareGetById(base, UserHeapPointer<HeapString> { index }, c_info /*out*/);
            bool c_mayHaveMt = c_info.m_mayHaveMetatable;
            if (c_info.m_icKind == GetByIdICInfo::ICKind::InlinedStorage)
            {
                int32_t c_slot = c_info.m_slot;
                return ic->Effect([base, c_slot, c_mayHaveMt] {
                    IcSpecializeValueFullCoverage(c_mayHaveMt, false, true);
                    return std::make_pair(TCGet(base->m_inlineStorage[c_slot]), c_mayHaveMt);
                });
            }
            else if (c_info.m_icKind == GetByIdICInfo::ICKind::OutlinedStorage)
            {
                int32_t c_slot = c_info.m_slot;
                return ic->Effect([base, c_slot, c_mayHaveMt] {
                    IcSpecializeValueFullCoverage(c_mayHaveMt, false, true);
                    return std::make_pair(base->m_butterfly->GetNamedProperty(c_slot), c_mayHaveMt);
                });
            }
            else
            {
                assert(c_info.m_icKind == GetByIdICInfo::ICKind::MustBeNilButUncacheable);
                return std::make_pair(TValue::Nil(), c_mayHaveMt);
            }
        });

        if (likely(!mayHaveMt || !result.Is<tNil>()))
        {
            Return(result);
        }
    }

    EnterSlowPath([base, index]() NO_RETURN mutable {
check_metatable:
        TValue metamethodBase;
        TValue metamethod;

        TableObject::GetMetatableResult gmr = TableObject::GetMetatable(base);
        if (gmr.m_result.m_value != 0)
        {
            HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
            metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Index);
            if (!metamethod.Is<tNil>())
            {
                metamethodBase = TValue::Create<tTable>(base);
                goto handle_metamethod;
            }
        }
        Return(TValue::Create<tNil>());

handle_metamethod:
        // If 'metamethod' is a function, we should invoke the metamethod, throwing out an error if fail
        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        if (likely(metamethod.Is<tHeapEntity>()))
        {
            HeapEntityType mmType = metamethod.GetHeapEntityType();
            if (mmType == HeapEntityType::Function)
            {
                MakeCall(metamethod.As<tFunction>(), metamethodBase, TValue::Create<tString>(index), GlobalGetMetamethodCallContinuation);
            }
            else if (mmType == HeapEntityType::Table)
            {
                base = metamethod.As<tTable>();
                GetByIdICInfo icInfo;
                TableObject::PrepareGetById(base, UserHeapPointer<HeapString> { index }, icInfo /*out*/);
                TValue result = TableObject::GetById(base, index, icInfo);
                if (likely(!icInfo.m_mayHaveMetatable || !result.Is<tNil>()))
                {
                    Return(result);
                }
                goto check_metatable;
            }
        }

        // Now we know 'metamethod' is not a function or table, so we should locate its own exotic '__index' metamethod..
        // The difference is that if the metamethod is nil, we need to throw an error
        //
        metamethodBase = metamethod;
        metamethod = GetMetamethodForValue(metamethod, LuaMetamethodKind::Index);
        if (metamethod.Is<tNil>())
        {
            // TODO: make error message consistent with Lua
            //
            ThrowError("bad type for GlobalGet");
        }
        goto handle_metamethod;
    });
}

DEEGEN_DEFINE_BYTECODE(GlobalGet)
{
    Operands(
        Constant("index")
    );
    Result(BytecodeValue);
    Implementation(GlobalGetImpl);
    Variant(
        Op("index").IsConstant<tString>()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
