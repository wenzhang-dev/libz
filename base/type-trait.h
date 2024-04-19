#pragma once

#include <deque>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace libz {
namespace _ {

template <template <typename...> typename Template, typename Type>
struct TemplateInstance : std::false_type {};

template <template <typename...> typename Template, typename... Type>
struct TemplateInstance<Template, Template<Type...>> : std::true_type {};

template <typename, typename = void>
struct IsContainerImpl : std::false_type {};

template <typename T>
struct IsContainerImpl<
    T, std::void_t<decltype(std::declval<T>().size(), std::declval<T>().begin(),
                            std::declval<T>().end())>> : std::true_type {};
template <typename T>
struct IsSequenceContainerImpl
    : std::integral_constant<bool,
                             TemplateInstance<std::deque, T>::value ||
                                 TemplateInstance<std::list, T>::value ||
                                 TemplateInstance<std::vector, T>::value ||
                                 TemplateInstance<std::queue, T>::value> {};

template <typename T>
struct IsAssciativeContainerImpl
    : std::integral_constant<
          bool, TemplateInstance<std::map, T>::value ||
                    TemplateInstance<std::unordered_map, T>::value ||
                    TemplateInstance<std::set, T>::value ||
                    TemplateInstance<std::unordered_set, T>::value> {};

template <typename, typename = void>
struct IsMapContainerImpl : std::false_type {};

// Notes, both of std::map and std::unordered_map have the `mapped_type`
// defination
template <typename T>
struct IsMapContainerImpl<
    T, std::void_t<decltype(std::declval<typename T::mapped_type>())>>
    : IsContainerImpl<T> {};

template <typename T>
struct IsSetContainerImpl
    : std::integral_constant<
          bool, TemplateInstance<std::set, T>::value ||
                    TemplateInstance<std::unordered_set, T>::value> {};

template <typename T, typename = void>
struct IsArrayImpl : std::false_type {};

template <typename T>
struct IsArrayImpl<
    T, std::void_t<decltype(std::declval<T>().size()),
                   typename std::enable_if_t<(std::tuple_size<T>::value != 0)>>>
    : std::true_type {};

}  // namespace _

template <template <typename...> typename Template, typename Type>
inline constexpr bool IsTemplateOf = _::TemplateInstance<Template, Type>::value;

template <typename T>
using RemoveCVRef =
    typename std::remove_cv<typename std::remove_reference<T>::type>::type;

template <typename T>
inline constexpr bool IsBool = std::is_same_v<std::decay_t<T>, bool>;

template <typename T>
inline constexpr bool IsChar = std::is_same_v<std::decay_t<T>, char> ||
                               std::is_same_v<std::decay_t<T>, char16_t> ||
                               std::is_same_v<std::decay_t<T>, char32_t> ||
                               std::is_same_v<std::decay_t<T>, wchar_t>;

template <typename T>
inline constexpr bool IsIntegral =
    std::is_integral_v<std::decay_t<T>> && !IsChar<T> &&
    !std::is_same_v<std::decay_t<T>, bool>;

template <typename T>
inline constexpr bool IsFloat = std::is_floating_point_v<std::decay_t<T>>;

template <typename T>
inline constexpr bool IsNumeric = IsIntegral<T> || IsFloat<T>;

template <typename T>
inline constexpr bool IsEnum = std::is_enum_v<T>;

// Notes, c-style array should not be empty
template <class T>
inline constexpr bool IsCArray =
    std::is_array_v<RemoveCVRef<T>>&& std::extent<RemoveCVRef<T>>::value > 0;

// notes, array includes std::pair and std::array
template <typename T>
inline constexpr bool IsArray = _::IsArrayImpl<RemoveCVRef<T>>::value;

template <typename T>
inline constexpr bool IsFixedArray = IsCArray<T> || IsArray<T>;

// Notes, we expect the tuple should not be empty
template <typename T>
inline constexpr bool IsTuple = IsTemplateOf<std::tuple, RemoveCVRef<T>>;

template <typename T>
inline constexpr bool IsString =
    IsTemplateOf<std::basic_string, RemoveCVRef<T>>;

template <typename T>
inline constexpr bool IsStringView =
    IsTemplateOf<std::basic_string_view, RemoveCVRef<T>>;

template <typename T>
constexpr inline bool IsStringLike = IsString<T> || IsStringView<T>;

template <typename T>
inline constexpr bool IsContainer = _::IsContainerImpl<RemoveCVRef<T>>::value;

template <typename T>
inline constexpr bool IsMapContainer =
    _::IsMapContainerImpl<RemoveCVRef<T>>::value;

template <typename T>
inline constexpr bool IsSetContainer =
    _::IsSetContainerImpl<RemoveCVRef<T>>::value;

template <typename T>
inline constexpr bool IsSequenceContainer =
    _::IsSequenceContainerImpl<RemoveCVRef<T>>::value;

template <typename T>
inline constexpr bool IsAssciativeContainer =
    _::IsAssciativeContainerImpl<RemoveCVRef<T>>::value;

template <typename T>
inline constexpr bool IsVariant = IsTemplateOf<std::variant, RemoveCVRef<T>>;

template <typename T>
inline constexpr bool IsOptional = IsTemplateOf<std::optional, RemoveCVRef<T>>;

namespace _ {

template <typename T, typename = void>
struct IsCharArrayImpl : std::false_type {};

template <typename T>
struct IsCharArrayImpl<
    T, std::void_t<
           std::enable_if_t<IsFixedArray<T>>,
           std::enable_if_t<std::is_same_v<
               char, std::remove_reference_t<decltype(std::declval<T>()[0])>>>>>
    : std::true_type {};

template <typename T, typename = void>
struct IsNonCharArrayImpl : std::false_type {};

template <typename T>
struct IsNonCharArrayImpl<
    T, std::void_t<
           std::enable_if_t<IsFixedArray<T>>,
           std::enable_if_t<!std::is_same_v<
               char, std::remove_reference_t<decltype(std::declval<T>()[0])>>>>>
    : std::true_type {};
}  // namespace _

template <typename T>
inline constexpr bool IsCharArray = _::IsCharArrayImpl<T>::value;

template <typename T>
inline constexpr bool IsNonCharArray = _::IsNonCharArrayImpl<T>::value;

template <typename T>
inline constexpr bool IsUniquePtr =
    IsTemplateOf<std::unique_ptr, RemoveCVRef<T>>;

template <typename T>
inline constexpr bool IsSharedPtr =
    IsTemplateOf<std::shared_ptr, RemoveCVRef<T>>;

template <typename T>
inline constexpr bool IsSmartPtr = IsUniquePtr<T> || IsSharedPtr<T>;

}  // namespace libz

