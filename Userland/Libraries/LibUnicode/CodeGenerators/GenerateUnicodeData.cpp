/*
 * Copyright (c) 2021, Tim Flynn <trflynn89@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AllOf.h>
#include <AK/Array.h>
#include <AK/CharacterTypes.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/QuickSort.h>
#include <AK/SourceGenerator.h>
#include <AK/String.h>
#include <AK/StringUtils.h>
#include <AK/Types.h>
#include <AK/Vector.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>

// Some code points are excluded from UnicodeData.txt, and instead are part of a "range" of code
// points, as indicated by the "name" field. For example:
//     3400;<CJK Ideograph Extension A, First>;Lo;0;L;;;;;N;;;;;
//     4DBF;<CJK Ideograph Extension A, Last>;Lo;0;L;;;;;N;;;;;
struct CodePointRange {
    u32 first;
    u32 last;
};

// SpecialCasing source: https://www.unicode.org/Public/13.0.0/ucd/SpecialCasing.txt
// Field descriptions: https://www.unicode.org/reports/tr44/tr44-13.html#SpecialCasing.txt
struct SpecialCasing {
    u32 index { 0 };
    u32 code_point { 0 };
    Vector<u32> lowercase_mapping;
    Vector<u32> uppercase_mapping;
    Vector<u32> titlecase_mapping;
    String locale;
    String condition;
};

// PropList source: https://www.unicode.org/Public/13.0.0/ucd/PropList.txt
// Property descriptions: https://www.unicode.org/reports/tr44/tr44-13.html#PropList.txt
//                        https://www.unicode.org/reports/tr44/tr44-13.html#WordBreakProperty.txt
using PropList = HashMap<String, Vector<CodePointRange>>;

// PropertyAliases source: https://www.unicode.org/Public/13.0.0/ucd/PropertyAliases.txt
struct Alias {
    String property;
    String alias;
};

// UnicodeData source: https://www.unicode.org/Public/13.0.0/ucd/UnicodeData.txt
// Field descriptions: https://www.unicode.org/reports/tr44/tr44-13.html#UnicodeData.txt
//                     https://www.unicode.org/reports/tr44/#General_Category_Values
struct CodePointData {
    u32 code_point { 0 };
    String name;
    String general_category;
    u8 canonical_combining_class { 0 };
    String bidi_class;
    String decomposition_type;
    Optional<i8> numeric_value_decimal;
    Optional<i8> numeric_value_digit;
    Optional<i8> numeric_value_numeric;
    bool bidi_mirrored { false };
    String unicode_1_name;
    String iso_comment;
    Optional<u32> simple_uppercase_mapping;
    Optional<u32> simple_lowercase_mapping;
    Optional<u32> simple_titlecase_mapping;
    Vector<u32> special_casing_indices;
    Vector<StringView> prop_list;
    StringView script;
    Vector<StringView> script_extensions;
    StringView word_break_property;
};

struct UnicodeData {
    Vector<SpecialCasing> special_casing;
    u32 largest_casing_transform_size { 0 };
    u32 largest_special_casing_size { 0 };
    Vector<String> locales;
    Vector<String> conditions;

    Vector<CodePointData> code_point_data;
    Vector<CodePointRange> code_point_ranges;

    // The Unicode standard defines General Category values which are not in any UCD file. These
    // values are simply unions of other values.
    // https://www.unicode.org/reports/tr44/#GC_Values_Table
    Vector<String> general_categories;
    Vector<Alias> general_category_unions {
        { "Ll | Lu | Lt"sv, "LC"sv },
        { "Lu | Ll | Lt | Lm | Lo"sv, "L"sv },
        { "Mn | Mc | Me"sv, "M"sv },
        { "Nd | Nl | No"sv, "N"sv },
        { "Pc | Pd | Ps | Pe | Pi | Pf | Po"sv, "P"sv },
        { "Sm | Sc | Sk | So"sv, "S"sv },
        { "Zs | Zl | Zp"sv, "Z"sv },
        { "Cc | Cf | Cs | Co"sv, "C"sv }, // FIXME: This union should also contain "Cn" (Unassigned), which we don't parse yet.
    };
    Vector<Alias> general_category_aliases;

    // The Unicode standard defines additional properties (Any, Assigned, ASCII) which are not in
    // any UCD file. Assigned is set as the default enum value 0 so "property & Assigned == Assigned"
    // is always true. Any is not assigned code points here because this file only parses assigned
    // code points, whereas Any will include unassigned code points.
    // https://unicode.org/reports/tr18/#General_Category_Property
    PropList prop_list {
        { "Any"sv, {} },
        { "ASCII"sv, { { 0, 0x7f } } },
    };
    Vector<Alias> prop_aliases;

    PropList script_list {
        { "Unknown"sv, {} },
    };
    Vector<Alias> script_aliases;
    PropList script_extensions;
    u32 largest_script_extensions_size { 0 };

    PropList word_break_prop_list;
};

static constexpr auto s_desired_fields = Array {
    "general_category"sv,
    "simple_uppercase_mapping"sv,
    "simple_lowercase_mapping"sv,
};

static void write_to_file_if_different(Core::File& file, StringView contents)
{
    auto const current_contents = file.read_all();

    if (StringView { current_contents.bytes() } == contents)
        return;

    VERIFY(file.seek(0));
    VERIFY(file.truncate(0));
    VERIFY(file.write(contents));
}

static void parse_special_casing(Core::File& file, UnicodeData& unicode_data)
{
    auto parse_code_point_list = [&](auto const& line) {
        Vector<u32> code_points;

        auto segments = line.split(' ');
        for (auto const& code_point : segments)
            code_points.append(AK::StringUtils::convert_to_uint_from_hex<u32>(code_point).value());

        return code_points;
    };

    while (file.can_read_line()) {
        auto line = file.read_line();
        if (line.is_empty() || line.starts_with('#'))
            continue;

        if (auto index = line.find('#'); index.has_value())
            line = line.substring(0, *index);

        auto segments = line.split(';', true);
        VERIFY(segments.size() == 5 || segments.size() == 6);

        SpecialCasing casing {};
        casing.index = static_cast<u32>(unicode_data.special_casing.size());
        casing.code_point = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[0]).value();
        casing.lowercase_mapping = parse_code_point_list(segments[1]);
        casing.titlecase_mapping = parse_code_point_list(segments[2]);
        casing.uppercase_mapping = parse_code_point_list(segments[3]);

        if (auto condition = segments[4].trim_whitespace(); !condition.is_empty()) {
            auto conditions = condition.split(' ', true);
            VERIFY(conditions.size() == 1 || conditions.size() == 2);

            if (conditions.size() == 2) {
                casing.locale = move(conditions[0]);
                casing.condition = move(conditions[1]);
            } else if (all_of(conditions[0], is_ascii_lower_alpha)) {
                casing.locale = move(conditions[0]);
            } else {
                casing.condition = move(conditions[0]);
            }

            casing.locale = casing.locale.to_uppercase();
            casing.condition.replace("_", "", true);

            if (!casing.locale.is_empty() && !unicode_data.locales.contains_slow(casing.locale))
                unicode_data.locales.append(casing.locale);
            if (!casing.condition.is_empty() && !unicode_data.conditions.contains_slow(casing.condition))
                unicode_data.conditions.append(casing.condition);
        }

        unicode_data.largest_casing_transform_size = max(unicode_data.largest_casing_transform_size, casing.lowercase_mapping.size());
        unicode_data.largest_casing_transform_size = max(unicode_data.largest_casing_transform_size, casing.titlecase_mapping.size());
        unicode_data.largest_casing_transform_size = max(unicode_data.largest_casing_transform_size, casing.uppercase_mapping.size());

        unicode_data.special_casing.append(move(casing));
    }
}

static void parse_prop_list(Core::File& file, PropList& prop_list, bool multi_value_property = false)
{
    while (file.can_read_line()) {
        auto line = file.read_line();
        if (line.is_empty() || line.starts_with('#'))
            continue;

        if (auto index = line.find('#'); index.has_value())
            line = line.substring(0, *index);

        auto segments = line.split_view(';', true);
        VERIFY(segments.size() == 2);

        auto code_point_range = segments[0].trim_whitespace();
        Vector<StringView> properties;

        if (multi_value_property)
            properties = segments[1].trim_whitespace().split_view(' ');
        else
            properties = { segments[1].trim_whitespace() };

        for (auto const& property : properties) {
            auto& code_points = prop_list.ensure(property.trim_whitespace());

            if (code_point_range.contains(".."sv)) {
                segments = code_point_range.split_view(".."sv);
                VERIFY(segments.size() == 2);

                auto begin = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[0]).value();
                auto end = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[1]).value();
                code_points.append({ begin, end });
            } else {
                auto code_point = AK::StringUtils::convert_to_uint_from_hex<u32>(code_point_range).value();
                code_points.append({ code_point, code_point });
            }
        }
    }
}

static void parse_alias_list(Core::File& file, PropList const& prop_list, Vector<Alias>& prop_aliases)
{
    String current_property;

    auto append_alias = [&](auto alias, auto property) {
        // Note: The alias files contain lines such as "Hyphen = Hyphen", which we should just skip.
        if (alias == property)
            return;

        // FIXME: We will, eventually, need to find where missing properties are located and parse them.
        if (!prop_list.contains(property))
            return;

        prop_aliases.append({ property, alias });
    };

    while (file.can_read_line()) {
        auto line = file.read_line();
        if (line.is_empty() || line.starts_with('#')) {
            if (line.ends_with("Properties"sv))
                current_property = line.substring(2);
            continue;
        }

        // Note: For now, we only care about Binary Property aliases for Unicode property escapes.
        if (current_property != "Binary Properties"sv)
            continue;

        auto segments = line.split_view(';', true);
        VERIFY((segments.size() == 2) || (segments.size() == 3));

        auto alias = segments[0].trim_whitespace();
        auto property = segments[1].trim_whitespace();
        append_alias(alias, property);

        if (segments.size() == 3) {
            alias = segments[2].trim_whitespace();
            append_alias(alias, property);
        }
    }
}

static void parse_value_alias_list(Core::File& file, StringView desired_category, Vector<String> const& value_list, Vector<Alias> const& prop_unions, Vector<Alias>& prop_aliases, bool primary_value_is_first = true)
{
    VERIFY(file.seek(0));

    auto append_alias = [&](auto alias, auto value) {
        // Note: The value alias file contains lines such as "Ahom = Ahom", which we should just skip.
        if (alias == value)
            return;

        // FIXME: We will, eventually, need to find where missing properties are located and parse them.
        if (!value_list.contains_slow(value) && !any_of(prop_unions, [&](auto const& u) { return value == u.alias; }))
            return;

        prop_aliases.append({ value, alias });
    };

    while (file.can_read_line()) {
        auto line = file.read_line();
        if (line.is_empty() || line.starts_with('#'))
            continue;

        if (auto index = line.find('#'); index.has_value())
            line = line.substring(0, *index);

        auto segments = line.split_view(';', true);
        auto category = segments[0].trim_whitespace();

        if (category != desired_category)
            continue;

        VERIFY((segments.size() == 3) || (segments.size() == 4));
        auto value = primary_value_is_first ? segments[1].trim_whitespace() : segments[2].trim_whitespace();
        auto alias = primary_value_is_first ? segments[2].trim_whitespace() : segments[1].trim_whitespace();
        append_alias(alias, value);

        if (segments.size() == 4) {
            alias = segments[3].trim_whitespace();
            append_alias(alias, value);
        }
    }
}

static void parse_unicode_data(Core::File& file, UnicodeData& unicode_data)
{
    Optional<u32> code_point_range_start;

    auto assign_code_point_property = [&](u32 code_point, auto const& list, auto& property, StringView default_) {
        using PropertyType = RemoveCVReference<decltype(property)>;
        constexpr bool is_single_item = IsSame<PropertyType, StringView>;

        auto assign_property = [&](auto const& item) {
            if constexpr (is_single_item)
                property = item;
            else
                property.append(item);
        };

        for (auto const& item : list) {
            for (auto const& range : item.value) {
                if ((range.first <= code_point) && (code_point <= range.last)) {
                    assign_property(item.key);
                    break;
                }
            }
            if constexpr (is_single_item) {
                if (!property.is_empty())
                    break;
            }
        }

        if (property.is_empty() && !default_.is_empty())
            assign_property(default_);
    };

    while (file.can_read_line()) {
        auto line = file.read_line();
        if (line.is_empty())
            continue;

        auto segments = line.split(';', true);
        VERIFY(segments.size() == 15);

        CodePointData data {};
        data.code_point = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[0]).value();
        data.name = move(segments[1]);
        data.general_category = move(segments[2]);
        data.canonical_combining_class = AK::StringUtils::convert_to_uint<u8>(segments[3]).value();
        data.bidi_class = move(segments[4]);
        data.decomposition_type = move(segments[5]);
        data.numeric_value_decimal = AK::StringUtils::convert_to_int<i8>(segments[6]);
        data.numeric_value_digit = AK::StringUtils::convert_to_int<i8>(segments[7]);
        data.numeric_value_numeric = AK::StringUtils::convert_to_int<i8>(segments[8]);
        data.bidi_mirrored = segments[9] == "Y"sv;
        data.unicode_1_name = move(segments[10]);
        data.iso_comment = move(segments[11]);
        data.simple_uppercase_mapping = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[12]);
        data.simple_lowercase_mapping = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[13]);
        data.simple_titlecase_mapping = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[14]);

        if (data.name.starts_with("<"sv) && data.name.ends_with(", First>")) {
            VERIFY(!code_point_range_start.has_value());
            code_point_range_start = data.code_point;

            data.name = data.name.substring(1, data.name.length() - 9);
        } else if (data.name.starts_with("<"sv) && data.name.ends_with(", Last>")) {
            VERIFY(code_point_range_start.has_value());

            unicode_data.code_point_ranges.append({ *code_point_range_start, data.code_point });
            data.name = data.name.substring(1, data.name.length() - 8);

            code_point_range_start.clear();
        }

        for (auto const& casing : unicode_data.special_casing) {
            if (casing.code_point == data.code_point)
                data.special_casing_indices.append(casing.index);
        }

        assign_code_point_property(data.code_point, unicode_data.prop_list, data.prop_list, "Assigned"sv);
        assign_code_point_property(data.code_point, unicode_data.script_list, data.script, "Unknown"sv);
        assign_code_point_property(data.code_point, unicode_data.script_extensions, data.script_extensions, {});
        assign_code_point_property(data.code_point, unicode_data.word_break_prop_list, data.word_break_property, "Other"sv);

        unicode_data.largest_special_casing_size = max(unicode_data.largest_special_casing_size, data.special_casing_indices.size());
        unicode_data.largest_script_extensions_size = max(unicode_data.largest_script_extensions_size, data.script_extensions.size());

        if (!unicode_data.general_categories.contains_slow(data.general_category))
            unicode_data.general_categories.append(data.general_category);

        unicode_data.code_point_data.append(move(data));
    }
}

static void generate_unicode_data_header(Core::File& file, UnicodeData& unicode_data)
{
    StringBuilder builder;
    SourceGenerator generator { builder };
    generator.set("casing_transform_size", String::number(unicode_data.largest_casing_transform_size));
    generator.set("special_casing_size", String::number(unicode_data.largest_special_casing_size));
    generator.set("script_extensions_size", String::number(unicode_data.largest_script_extensions_size));

    auto generate_enum = [&](StringView name, StringView default_, Vector<String> values, Vector<Alias> unions = {}, Vector<Alias> aliases = {}, bool as_bitmask = false) {
        VERIFY(!as_bitmask || (values.size() <= 64));
        quick_sort(values);
        quick_sort(unions, [](auto& union1, auto& union2) { return union1.alias < union2.alias; });
        quick_sort(aliases, [](auto& alias1, auto& alias2) { return alias1.alias < alias2.alias; });

        generator.set("name", name);
        generator.set("underlying", String::formatted("{}UnderlyingType", name));

        if (as_bitmask) {
            generator.append(R"~~~(
using @underlying@ = u64;

enum class @name@ : @underlying@ {)~~~");
        } else {
            generator.append(R"~~~(
enum class @name@ {)~~~");
        }

        if (!default_.is_empty()) {
            generator.set("default", default_);
            generator.append(R"~~~(
    @default@,)~~~");
        }

        u8 index = 0;
        for (auto const& value : values) {
            generator.set("value", value);

            if (as_bitmask) {
                generator.set("index", String::number(index++));
                generator.append(R"~~~(
    @value@ = static_cast<@underlying@>(1) << @index@,)~~~");
            } else {
                generator.append(R"~~~(
    @value@,)~~~");
            }
        }

        for (auto const& union_ : unions) {
            generator.set("union", union_.alias);
            generator.set("value", union_.property);
            generator.append(R"~~~(
    @union@ = @value@,)~~~");
        }
        for (auto const& alias : aliases) {
            generator.set("alias", alias.alias);
            generator.set("value", alias.property);
            generator.append(R"~~~(
    @alias@ = @value@,)~~~");
        }

        generator.append(R"~~~(
};
)~~~");

        if (as_bitmask) {
            generator.append(R"~~~(
constexpr @name@ operator&(@name@ value1, @name@ value2)
{
    return static_cast<@name@>(static_cast<@underlying@>(value1) & static_cast<@underlying@>(value2));
}

constexpr @name@ operator|(@name@ value1, @name@ value2)
{
    return static_cast<@name@>(static_cast<@underlying@>(value1) | static_cast<@underlying@>(value2));
}
)~~~");
        }
    };

    generator.append(R"~~~(
#pragma once

#include <AK/Optional.h>
#include <AK/Types.h>
#include <LibUnicode/Forward.h>

namespace Unicode {
)~~~");

    generate_enum("Locale"sv, "None"sv, move(unicode_data.locales));
    generate_enum("Condition"sv, "None"sv, move(unicode_data.conditions));
    generate_enum("GeneralCategory"sv, "None"sv, unicode_data.general_categories, unicode_data.general_category_unions, unicode_data.general_category_aliases, true);
    generate_enum("Property"sv, "Assigned"sv, unicode_data.prop_list.keys(), {}, unicode_data.prop_aliases, true);
    generate_enum("Script"sv, {}, unicode_data.script_list.keys(), {}, unicode_data.script_aliases);
    generate_enum("WordBreakProperty"sv, "Other"sv, unicode_data.word_break_prop_list.keys());

    generator.append(R"~~~(
struct SpecialCasing {
    u32 code_point { 0 };

    u32 lowercase_mapping[@casing_transform_size@];
    u32 lowercase_mapping_size { 0 };

    u32 uppercase_mapping[@casing_transform_size@];
    u32 uppercase_mapping_size { 0 };

    u32 titlecase_mapping[@casing_transform_size@];
    u32 titlecase_mapping_size { 0 };

    Locale locale { Locale::None };
    Condition condition { Condition::None };
};

struct UnicodeData {
    u32 code_point;)~~~");

    auto append_field = [&](StringView type, StringView name) {
        if (!s_desired_fields.span().contains_slow(name))
            return;

        generator.set("type", type);
        generator.set("name", name);
        generator.append(R"~~~(
    @type@ @name@;)~~~");
    };

    // Note: For compile-time performance, only primitive types are used.
    append_field("char const*"sv, "name"sv);
    append_field("GeneralCategory"sv, "general_category"sv);
    append_field("u8"sv, "canonical_combining_class"sv);
    append_field("char const*"sv, "bidi_class"sv);
    append_field("char const*"sv, "decomposition_type"sv);
    append_field("i8"sv, "numeric_value_decimal"sv);
    append_field("i8"sv, "numeric_value_digit"sv);
    append_field("i8"sv, "numeric_value_numeric"sv);
    append_field("bool"sv, "bidi_mirrored"sv);
    append_field("char const*"sv, "unicode_1_name"sv);
    append_field("char const*"sv, "iso_comment"sv);
    append_field("u32"sv, "simple_uppercase_mapping"sv);
    append_field("u32"sv, "simple_lowercase_mapping"sv);
    append_field("u32"sv, "simple_titlecase_mapping"sv);

    generator.append(R"~~~(

    SpecialCasing const* special_casing[@special_casing_size@] {};
    u32 special_casing_size { 0 };

    Property properties { Property::Assigned };

    Script script { Script::Unknown };
    Script script_extensions[@script_extensions_size@];
    u32 script_extensions_size { 0 };

    WordBreakProperty word_break_property { WordBreakProperty::Other };
};

namespace Detail {

Optional<UnicodeData> unicode_data_for_code_point(u32 code_point);
Optional<Property> property_from_string(StringView const& property);
Optional<GeneralCategory> general_category_from_string(StringView const& general_category);
Optional<Script> script_from_string(StringView const& script);

}

}
)~~~");

    write_to_file_if_different(file, generator.as_string_view());
}

static void generate_unicode_data_implementation(Core::File& file, UnicodeData const& unicode_data)
{
    StringBuilder builder;
    SourceGenerator generator { builder };

    generator.set("special_casing_size", String::number(unicode_data.special_casing.size()));
    generator.set("code_point_data_size", String::number(unicode_data.code_point_data.size()));

    generator.append(R"~~~(
#include <AK/Array.h>
#include <AK/CharacterTypes.h>
#include <AK/HashMap.h>
#include <AK/StringView.h>
#include <LibUnicode/UnicodeData.h>

namespace Unicode {
)~~~");

    auto append_list_and_size = [&](auto const& list, StringView format) {
        if (list.is_empty()) {
            generator.append(", {}, 0");
            return;
        }

        bool first = true;
        generator.append(", {");
        for (auto const& item : list) {
            generator.append(first ? " " : ", ");
            generator.append(String::formatted(format, item));
            first = false;
        }
        generator.append(String::formatted(" }}, {}", list.size()));
    };

    generator.append(R"~~~(
static constexpr Array<SpecialCasing, @special_casing_size@> s_special_casing { {)~~~");

    for (auto const& casing : unicode_data.special_casing) {
        generator.set("code_point", String::formatted("{:#x}", casing.code_point));
        generator.append(R"~~~(
    { @code_point@)~~~");

        constexpr auto format = "0x{:x}"sv;
        append_list_and_size(casing.lowercase_mapping, format);
        append_list_and_size(casing.uppercase_mapping, format);
        append_list_and_size(casing.titlecase_mapping, format);

        generator.set("locale", casing.locale.is_empty() ? "None" : casing.locale);
        generator.append(", Locale::@locale@");

        generator.set("condition", casing.condition.is_empty() ? "None" : casing.condition);
        generator.append(", Condition::@condition@");

        generator.append(" },");
    }

    generator.append(R"~~~(
} };

static constexpr Array<UnicodeData, @code_point_data_size@> s_unicode_data { {)~~~");

    auto append_field = [&](StringView name, String value) {
        if (!s_desired_fields.span().contains_slow(name))
            return;

        generator.set("value", move(value));
        generator.append(", @value@");
    };

    for (auto const& data : unicode_data.code_point_data) {
        generator.set("code_point", String::formatted("{:#x}", data.code_point));
        generator.append(R"~~~(
    { @code_point@)~~~");

        append_field("name", String::formatted("\"{}\"", data.name));
        append_field("general_category", String::formatted("GeneralCategory::{}", data.general_category));
        append_field("canonical_combining_class", String::number(data.canonical_combining_class));
        append_field("bidi_class", String::formatted("\"{}\"", data.bidi_class));
        append_field("decomposition_type", String::formatted("\"{}\"", data.decomposition_type));
        append_field("numeric_value_decimal", String::number(data.numeric_value_decimal.value_or(-1)));
        append_field("numeric_value_digit", String::number(data.numeric_value_digit.value_or(-1)));
        append_field("numeric_value_numeric", String::number(data.numeric_value_numeric.value_or(-1)));
        append_field("bidi_mirrored", String::formatted("{}", data.bidi_mirrored));
        append_field("unicode_1_name", String::formatted("\"{}\"", data.unicode_1_name));
        append_field("iso_comment", String::formatted("\"{}\"", data.iso_comment));
        append_field("simple_uppercase_mapping", String::formatted("{:#x}", data.simple_uppercase_mapping.value_or(data.code_point)));
        append_field("simple_lowercase_mapping", String::formatted("{:#x}", data.simple_lowercase_mapping.value_or(data.code_point)));
        append_field("simple_titlecase_mapping", String::formatted("{:#x}", data.simple_titlecase_mapping.value_or(data.code_point)));
        append_list_and_size(data.special_casing_indices, "&s_special_casing[{}]"sv);

        bool first = true;
        for (auto const& property : data.prop_list) {
            generator.append(first ? ", " : " | ");
            generator.append(String::formatted("Property::{}", property));
            first = false;
        }

        generator.append(String::formatted(", Script::{}", data.script));
        append_list_and_size(data.script_extensions, "Script::{}"sv);
        generator.append(String::formatted(", WordBreakProperty::{}", data.word_break_property));
        generator.append(" },");
    }

    generator.append(R"~~~(
} };

static HashMap<u32, UnicodeData const*> const& ensure_code_point_map()
{
    static HashMap<u32, UnicodeData const*> code_point_to_data_map;
    code_point_to_data_map.ensure_capacity(s_unicode_data.size());

    for (auto const& unicode_data : s_unicode_data)
        code_point_to_data_map.set(unicode_data.code_point, &unicode_data);

    return code_point_to_data_map;
}

static Optional<u32> index_of_code_point_in_range(u32 code_point)
{)~~~");

    for (auto const& range : unicode_data.code_point_ranges) {
        generator.set("first", String::formatted("{:#x}", range.first));
        generator.set("last", String::formatted("{:#x}", range.last));

        generator.append(R"~~~(
    if ((code_point > @first@) && (code_point < @last@))
        return @first@;)~~~");
    }

    generator.append(R"~~~(
    return {};
}

namespace Detail {

Optional<UnicodeData> unicode_data_for_code_point(u32 code_point)
{
    static auto const& code_point_to_data_map = ensure_code_point_map();
    VERIFY(is_unicode(code_point));

    if (auto data = code_point_to_data_map.get(code_point); data.has_value())
        return *(data.value());

    if (auto index = index_of_code_point_in_range(code_point); index.has_value()) {
        auto data_for_range = *(code_point_to_data_map.get(*index).value());
        data_for_range.simple_uppercase_mapping = code_point;
        data_for_range.simple_lowercase_mapping = code_point;
        return data_for_range;
    }

    return {};
}

Optional<Property> property_from_string(StringView const& property)
{
    if (property == "Assigned"sv)
        return Property::Assigned;)~~~");

    for (auto const& property : unicode_data.prop_list) {
        generator.set("property", property.key);
        generator.append(R"~~~(
    if (property == "@property@"sv)
        return Property::@property@;)~~~");
    }
    for (auto const& alias : unicode_data.prop_aliases) {
        generator.set("property", alias.alias);
        generator.append(R"~~~(
    if (property == "@property@"sv)
        return Property::@property@;)~~~");
    }

    generator.append(R"~~~(
    return {};
}

Optional<GeneralCategory> general_category_from_string(StringView const& general_category)
{)~~~");

    for (auto const& general_category : unicode_data.general_categories) {
        generator.set("general_category", general_category);
        generator.append(R"~~~(
    if (general_category == "@general_category@"sv)
        return GeneralCategory::@general_category@;)~~~");
    }
    for (auto const& union_ : unicode_data.general_category_unions) {
        generator.set("general_category", union_.alias);
        generator.append(R"~~~(
    if (general_category == "@general_category@"sv)
        return GeneralCategory::@general_category@;)~~~");
    }
    for (auto const& alias : unicode_data.general_category_aliases) {
        generator.set("general_category", alias.alias);
        generator.append(R"~~~(
    if (general_category == "@general_category@"sv)
        return GeneralCategory::@general_category@;)~~~");
    }

    generator.append(R"~~~(
    return {};
}

Optional<Script> script_from_string(StringView const& script)
{)~~~");

    for (auto const& script : unicode_data.script_list) {
        generator.set("script", script.key);
        generator.append(R"~~~(
    if (script == "@script@"sv)
        return Script::@script@;)~~~");
    }
    for (auto const& alias : unicode_data.script_aliases) {
        generator.set("script", alias.alias);
        generator.append(R"~~~(
    if (script == "@script@"sv)
        return Script::@script@;)~~~");
    }

    generator.append(R"~~~(
    return {};
}

}

}
)~~~");

    write_to_file_if_different(file, generator.as_string_view());
}

int main(int argc, char** argv)
{
    char const* generated_header_path = nullptr;
    char const* generated_implementation_path = nullptr;
    char const* unicode_data_path = nullptr;
    char const* special_casing_path = nullptr;
    char const* prop_list_path = nullptr;
    char const* derived_core_prop_path = nullptr;
    char const* derived_binary_prop_path = nullptr;
    char const* prop_alias_path = nullptr;
    char const* prop_value_alias_path = nullptr;
    char const* scripts_path = nullptr;
    char const* script_extensions_path = nullptr;
    char const* word_break_path = nullptr;
    char const* emoji_data_path = nullptr;

    Core::ArgsParser args_parser;
    args_parser.add_option(generated_header_path, "Path to the Unicode Data header file to generate", "generated-header-path", 'h', "generated-header-path");
    args_parser.add_option(generated_implementation_path, "Path to the Unicode Data implementation file to generate", "generated-implementation-path", 'c', "generated-implementation-path");
    args_parser.add_option(unicode_data_path, "Path to UnicodeData.txt file", "unicode-data-path", 'u', "unicode-data-path");
    args_parser.add_option(special_casing_path, "Path to SpecialCasing.txt file", "special-casing-path", 's', "special-casing-path");
    args_parser.add_option(prop_list_path, "Path to PropList.txt file", "prop-list-path", 'p', "prop-list-path");
    args_parser.add_option(derived_core_prop_path, "Path to DerivedCoreProperties.txt file", "derived-core-prop-path", 'd', "derived-core-prop-path");
    args_parser.add_option(derived_binary_prop_path, "Path to DerivedBinaryProperties.txt file", "derived-binary-prop-path", 'b', "derived-binary-prop-path");
    args_parser.add_option(prop_alias_path, "Path to PropertyAliases.txt file", "prop-alias-path", 'a', "prop-alias-path");
    args_parser.add_option(prop_value_alias_path, "Path to PropertyValueAliases.txt file", "prop-value-alias-path", 'v', "prop-value-alias-path");
    args_parser.add_option(scripts_path, "Path to Scripts.txt file", "scripts-path", 'r', "scripts-path");
    args_parser.add_option(script_extensions_path, "Path to ScriptExtensions.txt file", "script-extensions-path", 'x', "script-extensions-path");
    args_parser.add_option(word_break_path, "Path to WordBreakProperty.txt file", "word-break-path", 'w', "word-break-path");
    args_parser.add_option(emoji_data_path, "Path to emoji-data.txt file", "emoji-data-path", 'e', "emoji-data-path");
    args_parser.parse(argc, argv);

    auto open_file = [&](StringView path, StringView flags, Core::OpenMode mode = Core::OpenMode::ReadOnly) {
        if (path.is_empty()) {
            warnln("{} is required", flags);
            args_parser.print_usage(stderr, argv[0]);
            exit(1);
        }

        auto file_or_error = Core::File::open(path, mode);
        if (file_or_error.is_error()) {
            warnln("Failed to open {}: {}", path, file_or_error.release_error());
            exit(1);
        }

        return file_or_error.release_value();
    };

    auto generated_header_file = open_file(generated_header_path, "-h/--generated-header-path", Core::OpenMode::ReadWrite);
    auto generated_implementation_file = open_file(generated_implementation_path, "-c/--generated-implementation-path", Core::OpenMode::ReadWrite);
    auto unicode_data_file = open_file(unicode_data_path, "-u/--unicode-data-path");
    auto special_casing_file = open_file(special_casing_path, "-s/--special-casing-path");
    auto prop_list_file = open_file(prop_list_path, "-p/--prop-list-path");
    auto derived_core_prop_file = open_file(derived_core_prop_path, "-d/--derived-core-prop-path");
    auto derived_binary_prop_file = open_file(derived_binary_prop_path, "-b/--derived-binary-prop-path");
    auto prop_alias_file = open_file(prop_alias_path, "-a/--prop-alias-path");
    auto prop_value_alias_file = open_file(prop_value_alias_path, "-v/--prop-value-alias-path");
    auto scripts_file = open_file(scripts_path, "-r/--scripts-path");
    auto script_extensions_file = open_file(script_extensions_path, "-x/--script-extensions-path");
    auto word_break_file = open_file(word_break_path, "-w/--word-break-path");
    auto emoji_data_file = open_file(emoji_data_path, "-e/--emoji-data-path");

    UnicodeData unicode_data {};
    parse_special_casing(special_casing_file, unicode_data);
    parse_prop_list(prop_list_file, unicode_data.prop_list);
    parse_prop_list(derived_core_prop_file, unicode_data.prop_list);
    parse_prop_list(derived_binary_prop_file, unicode_data.prop_list);
    parse_prop_list(emoji_data_file, unicode_data.prop_list);
    parse_alias_list(prop_alias_file, unicode_data.prop_list, unicode_data.prop_aliases);
    parse_prop_list(scripts_file, unicode_data.script_list);
    parse_prop_list(script_extensions_file, unicode_data.script_extensions, true);
    parse_prop_list(word_break_file, unicode_data.word_break_prop_list);

    parse_unicode_data(unicode_data_file, unicode_data);
    parse_value_alias_list(prop_value_alias_file, "gc"sv, unicode_data.general_categories, unicode_data.general_category_unions, unicode_data.general_category_aliases);
    parse_value_alias_list(prop_value_alias_file, "sc"sv, unicode_data.script_list.keys(), {}, unicode_data.script_aliases, false);

    generate_unicode_data_header(generated_header_file, unicode_data);
    generate_unicode_data_implementation(generated_implementation_file, unicode_data);

    return 0;
}
