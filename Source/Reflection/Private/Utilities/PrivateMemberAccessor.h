#pragma once

#include "Engine/Compatibility.h"

#if ENGINE_UE5
namespace PrivateMemberAccessor
{
	template <typename AccessorName>
	AccessorName::MemberType TMemberPointer;

	template <typename AccessorName, typename AccessorName::MemberType MemberPointer>
	struct TPointerInitializer
	{
		TPointerInitializer()
		{
			TMemberPointer<AccessorName> = MemberPointer;
		}

		static TPointerInitializer Instance;
	};

	template <typename AccessorName, typename AccessorName::MemberType MemberPointer>
	TPointerInitializer<AccessorName, MemberPointer> TPointerInitializer<AccessorName, MemberPointer>::Instance;

	template <typename AccessorName, typename ThisType, typename... ArgumentsType>
	decltype(auto) Access(ThisType&& This, ArgumentsType&&... Arguments)
	{
		if constexpr (std::is_pointer_v<std::remove_reference_t<ThisType>>)
		{
			if constexpr (std::is_member_function_pointer_v<typename AccessorName::MemberType>)
			{
				return (Forward<ThisType>(This)->*TMemberPointer<AccessorName>)(Forward<ArgumentsType>(Arguments)...);
			}
			else
			{
				return Forward<ThisType>(This)->*TMemberPointer<AccessorName>;
			}
		}
		else
		{
			if constexpr (std::is_member_function_pointer_v<typename AccessorName::MemberType>)
			{
				return (Forward<ThisType>(This).*TMemberPointer<AccessorName>)(Forward<ArgumentsType>(Arguments)...);
			}
			else
			{
				return Forward<ThisType>(This).*TMemberPointer<AccessorName>;
			}
		}
	}
}

#define DEFINE_PRIVATE_MEMBER_ACCESSOR(AccessorName, MemberPointer, ...) \
	struct AccessorName \
	{ \
		using MemberType = __VA_ARGS__; \
		\
		template <typename ThisType, typename... ArgumentsType> \
		static decltype(auto) Access(ThisType&& This, ArgumentsType&&... Arguments) \
		{ \
			return PrivateMemberAccessor::Access<AccessorName>(Forward<ThisType>(This), Forward<ArgumentsType>(Arguments)...); \
		} \
	}; \
	\
	template struct PrivateMemberAccessor::TPointerInitializer<AccessorName, MemberPointer>;
#endif
