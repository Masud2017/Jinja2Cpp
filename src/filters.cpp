#include "filters.h"
#include "testers.h"
#include "value_visitors.h"
#include "value_helpers.h"

#include <algorithm>
#include <numeric>
#include <sstream>

namespace jinja2
{

template<typename F>
struct FilterFactory
{
    static FilterPtr Create(FilterParams params)
    {
        return std::make_shared<F>(std::move(params));
    }

    template<typename ... Args>
    static ExpressionFilter::FilterFactoryFn MakeCreator(Args&& ... args)
    {
        return [args...](FilterParams params) {return std::make_shared<F>(std::move(params), args...);};
    }
};

std::unordered_map<std::string, ExpressionFilter::FilterFactoryFn> s_filters = {
    {"attr", &FilterFactory<filters::Attribute>::Create},
    {"batch", FilterFactory<filters::Slice>::MakeCreator(filters::Slice::BatchMode)},
    {"camelize", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::CamelMode)},
    {"capitalize", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::CapitalMode)},
    {"default", &FilterFactory<filters::Default>::Create},
    {"d", &FilterFactory<filters::Default>::Create},
    {"dictsort", &FilterFactory<filters::DictSort>::Create},
    {"escape", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::EscapeHtmlMode)},
    {"escapecpp", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::EscapeCppMode)},
    {"first", FilterFactory<filters::SequenceAccessor>::MakeCreator(filters::SequenceAccessor::FirstItemMode)},
    {"float", FilterFactory<filters::ValueConverter>::MakeCreator(filters::ValueConverter::ToFloatMode)},
    {"format", FilterFactory<filters::StringFormat>::MakeCreator(filters::StringFormat::PythonMode)},
    {"groupby", &FilterFactory<filters::GroupBy>::Create},
    {"int", FilterFactory<filters::ValueConverter>::MakeCreator(filters::ValueConverter::ToIntMode)},
    {"join", &FilterFactory<filters::Join>::Create},
    {"last", FilterFactory<filters::SequenceAccessor>::MakeCreator(filters::SequenceAccessor::LastItemMode)},
    {"length", FilterFactory<filters::SequenceAccessor>::MakeCreator(filters::SequenceAccessor::LengthMode)},
    {"list", FilterFactory<filters::ValueConverter>::MakeCreator(filters::ValueConverter::ToListMode)},
    {"map", &FilterFactory<filters::Map>::Create},
    {"max", FilterFactory<filters::SequenceAccessor>::MakeCreator(filters::SequenceAccessor::MaxItemMode)},
    {"min", FilterFactory<filters::SequenceAccessor>::MakeCreator(filters::SequenceAccessor::MinItemMode)},
    {"pprint", &FilterFactory<filters::PrettyPrint>::Create},
    {"random", &FilterFactory<filters::Random>::Create},
    {"reject", FilterFactory<filters::Tester>::MakeCreator(filters::Tester::RejectMode)},
    {"rejectattr", FilterFactory<filters::Tester>::MakeCreator(filters::Tester::RejectAttrMode)},
    {"replace", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::ReplaceMode)},
    {"round", FilterFactory<filters::ValueConverter>::MakeCreator(filters::ValueConverter::RoundMode)},
    {"reverse", FilterFactory<filters::SequenceAccessor>::MakeCreator(filters::SequenceAccessor::ReverseMode)},
    {"select", FilterFactory<filters::Tester>::MakeCreator(filters::Tester::SelectMode)},
    {"selectattr", FilterFactory<filters::Tester>::MakeCreator(filters::Tester::SelectAttrMode)},
    {"slice", FilterFactory<filters::Slice>::MakeCreator(filters::Slice::SliceMode)},
    {"sort", &FilterFactory<filters::Sort>::Create},
    {"sum", FilterFactory<filters::SequenceAccessor>::MakeCreator(filters::SequenceAccessor::SumItemsMode)},
    {"title", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::TitleMode)},
    {"tojson", FilterFactory<filters::Serialize>::MakeCreator(filters::Serialize::JsonMode)},
    {"toxml", FilterFactory<filters::Serialize>::MakeCreator(filters::Serialize::XmlMode)},
    {"toyaml", FilterFactory<filters::Serialize>::MakeCreator(filters::Serialize::YamlMode)},
    {"trim", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::TrimMode)},
    {"truncate", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::TruncateMode)},
    {"unique", FilterFactory<filters::SequenceAccessor>::MakeCreator(filters::SequenceAccessor::UniqueItemsMode)},
    {"upper", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::UpperMode)},
    {"wordcount", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::WordCountMode)},
    {"wordwrap", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::WordWrapMode)},
    {"underscorize", FilterFactory<filters::StringConverter>::MakeCreator(filters::StringConverter::UnderscoreMode)},};

extern FilterPtr CreateFilter(std::string filterName, CallParams params)
{
    auto p = s_filters.find(filterName);
    if (p == s_filters.end())
        return FilterPtr();

    return p->second(std::move(params));
}

namespace filters
{

bool FilterBase::ParseParams(const std::initializer_list<ArgumentInfo>& argsInfo, const CallParams& params)
{
    bool result = true;
    m_args = helpers::ParseCallParams(argsInfo, params, result);

    return result;
}

InternalValue FilterBase::GetArgumentValue(std::string argName, RenderContext& context, InternalValue defVal)
{
    auto argExpr = m_args[argName];
    return argExpr ? argExpr->Evaluate(context) : std::move(defVal);
}

Join::Join(FilterParams params)
{
    ParseParams({{"d", false, std::string()}, {"attribute"}}, params);
}

InternalValue Join::Filter(const InternalValue& baseVal, RenderContext& context)
{
    InternalValue attrName = GetArgumentValue("attribute", context);

    bool isConverted = false;
    ListAdapter values = ConvertToList(baseVal, attrName, isConverted);

    if (!isConverted)
        return InternalValue();

    bool isFirst = true;
    InternalValue result;
    InternalValue delimiter = m_args["d"]->Evaluate(context);
    for (const InternalValue& val : values)
    {
        if (isFirst)
            isFirst = false;
        else
            result = Apply2<visitors::StringJoiner>(result, delimiter);

        result = Apply2<visitors::StringJoiner>(result, val);
    }

    return result;
}

Sort::Sort(FilterParams params)
{
    ParseParams({{"reverse", false, InternalValue(false)}, {"case_sensitive", false, InternalValue(false)}, {"attribute", false}}, params);
}

InternalValue Sort::Filter(const InternalValue& baseVal, RenderContext& context)
{
    InternalValue attrName = GetArgumentValue("attribute", context);
    InternalValue isReverseVal = GetArgumentValue("reverse", context, InternalValue(false));
    InternalValue isCsVal = GetArgumentValue("case_sensitive", context, InternalValue(false));

    bool isConverted = false;
    ListAdapter origValues = ConvertToList(baseVal, isConverted);
    if (!isConverted)
        return InternalValue();
    InternalValueList values = origValues.ToValueList();

    BinaryExpression::Operation oper =
            ConvertToBool(isReverseVal) ? BinaryExpression::LogicalGt : BinaryExpression::LogicalLt;
    BinaryExpression::CompareType compType =
            ConvertToBool(isCsVal) ? BinaryExpression::CaseSensitive : BinaryExpression::CaseInsensitive;

    std::sort(values.begin(), values.end(), [&attrName, oper, compType](auto& val1, auto& val2) {
        InternalValue cmpRes;
        if (IsEmpty(attrName))
            cmpRes = Apply2<visitors::BinaryMathOperation>(val1, val2, oper, compType);
        else
            cmpRes = Apply2<visitors::BinaryMathOperation>(Subscript(val1, attrName), Subscript(val2, attrName), oper, compType);

        return ConvertToBool(cmpRes);
    });

    return ListAdapter::CreateAdapter(std::move(values));
}

Attribute::Attribute(FilterParams params)
{
    ParseParams({{"name", true}}, params);
}

InternalValue Attribute::Filter(const InternalValue& baseVal, RenderContext& context)
{
    InternalValue attrNameVal = GetArgumentValue("name", context);
    return Subscript(baseVal, attrNameVal);
}

Default::Default(FilterParams params)
{
    ParseParams({{"default_value", false, InternalValue(std::string(""))}, {"boolean", false, InternalValue(false)}}, params);
}

InternalValue Default::Filter(const InternalValue& baseVal, RenderContext& context)
{
    InternalValue defaultVal = GetArgumentValue("default_value", context);
    InternalValue conditionResult = GetArgumentValue("boolean", context);

    if (IsEmpty(baseVal))
        return defaultVal;

    if (ConvertToBool(conditionResult) && !ConvertToBool(baseVal))
        return defaultVal;

    return baseVal;
}

DictSort::DictSort(FilterParams params)
{
    ParseParams({{"case_sensitive", false}, {"by", false, std::string("key")}, {"reverse", false}}, params);
}

InternalValue DictSort::Filter(const InternalValue& baseVal, RenderContext& context)
{
    const MapAdapter* map = boost::get<MapAdapter>(&baseVal);
    if (map == nullptr)
        return InternalValue();

    InternalValue isReverseVal = GetArgumentValue("reverse", context, InternalValue(false));
    InternalValue isCsVal = GetArgumentValue("case_sensitive", context, InternalValue(false));
    InternalValue byVal = GetArgumentValue("by", context, InternalValue(std::string("key")));

    bool (*comparator)(const KeyValuePair& left, const KeyValuePair& right);

    if (AsString(byVal) == "key") // Sort by key
    {
        if (ConvertToBool(isCsVal))
        {
            comparator =  [](const KeyValuePair& left, const KeyValuePair& right)
            {
                return left.key < right.key;
            };
        }
        else
        {
            comparator = [](const KeyValuePair& left, const KeyValuePair& right)
            {
                return boost::lexicographical_compare(left.key, right.key, boost::algorithm::is_iless());
            };
        }
    }
    else if (AsString(byVal) == "value")
    {
        if (ConvertToBool(isCsVal))
        {
            comparator = [](const KeyValuePair& left, const KeyValuePair& right)
            {
                return ConvertToBool(Apply2<visitors::BinaryMathOperation>(left.value, right.value, BinaryExpression::LogicalLt, BinaryExpression::CaseSensitive));
            };
        }
        else
        {
            comparator = [](const KeyValuePair& left, const KeyValuePair& right)
            {
                return ConvertToBool(Apply2<visitors::BinaryMathOperation>(left.value, right.value, BinaryExpression::LogicalLt, BinaryExpression::CaseInsensitive));
            };
        }
    }
    else
        return InternalValue();

    std::vector<KeyValuePair> tempVector;
    tempVector.reserve(map->GetSize());
    for (int64_t idx = 0; idx < map->GetSize(); ++ idx)
    {
        auto val = map->GetValueByIndex(idx);
        auto& kvVal = boost::get<KeyValuePair>(val);
        tempVector.push_back(std::move(kvVal));
    }

    if (ConvertToBool(isReverseVal))
        std::sort(tempVector.begin(), tempVector.end(), [comparator](auto& l, auto& r) {return comparator(r, l);});
    else
        std::sort(tempVector.begin(), tempVector.end(), [comparator](auto& l, auto& r) {return comparator(l, r);});

    InternalValueList resultList(tempVector.begin(), tempVector.end());

    return InternalValue(ListAdapter::CreateAdapter(std::move(resultList)));
}

GroupBy::GroupBy(FilterParams params)
{

}

InternalValue GroupBy::Filter(const InternalValue& baseVal, RenderContext& context)
{
    return InternalValue();
}

Map::Map(FilterParams params)
{
    FilterParams newParams;

    if (params.kwParams.size() == 1 && params.posParams.empty() && params.kwParams.count("attribute") == 1)
    {
        newParams.kwParams["name"] = params.kwParams["attribute"];
        newParams.kwParams["filter"] = std::make_shared<ConstantExpression>(std::string("attr"));
    }
    else
    {
        newParams = std::move(params);
    }

    ParseParams({{"filter", true}}, newParams);
    m_mappingParams.kwParams = m_args.extraKwArgs;
    m_mappingParams.posParams = m_args.extraPosArgs;
}

InternalValue Map::Filter(const InternalValue& baseVal, RenderContext& context)
{
    InternalValue filterName = GetArgumentValue("filter", context);
    if (IsEmpty(filterName))
        return InternalValue();

    auto filter = CreateFilter(AsString(filterName), m_mappingParams);
    if (!filter)
        return InternalValue();

    bool isConverted = false;
    auto list = ConvertToList(baseVal, isConverted);
    if (!isConverted)
        return InternalValue();

    InternalValueList resultList;
    resultList.reserve(list.GetSize());
    std::transform(list.begin(), list.end(), std::back_inserter(resultList), [filter, &context](auto& val) {return filter->Filter(val, context);});

    return ListAdapter::CreateAdapter(std::move(resultList));
}

struct PrettyPrinter : visitors::BaseVisitor<InternalValue>
{
    using BaseVisitor::operator();

    PrettyPrinter(const RenderContext* context)
        : m_context(context)
    {}

    InternalValue operator()(const ListAdapter& list) const
    {
        std::ostringstream os;

        os << "[";
        bool isFirst = true;

        for (auto& v : list)
        {
            if (isFirst)
                isFirst = false;
            else
                os << ", ";
            os << AsString(Apply<PrettyPrinter>(v, m_context));
        }
        os << "]";

        return InternalValue(os.str());
    }

    InternalValue operator()(const MapAdapter& map) const
    {
        std::ostringstream os;
        os << "{";

        const auto& keys = map.GetKeys();

        bool isFirst = true;
        for (auto& k : keys)
        {
            if (isFirst)
                isFirst = false;
            else
                os << ", ";

            os << "'" << k << "': ";
            os << AsString(Apply<PrettyPrinter>(map.GetValueByName(k), m_context));
        }

        os << "}";

        return InternalValue(os.str());
    }

    InternalValue operator() (const KeyValuePair& kwPair) const
    {
        std::ostringstream os;

        os << "'" << kwPair.key << "': ";
        os << AsString(Apply<PrettyPrinter>(kwPair.value, m_context));

        return InternalValue(os.str());
    }

    InternalValue operator()(const std::string& str) const
    {
        return "'" + str + "'";
    }

    InternalValue operator()(const std::wstring& str) const
    {
        return std::string("'<wchar_string>'");
    }

    InternalValue operator()(bool val) const
    {
        return std::string(val ? "true" : "false");
    }

    InternalValue operator()(EmptyValue val) const
    {
        return std::string("none");
    }

    InternalValue operator()(double val) const
    {
        std::ostringstream os;
        os << val;
        return InternalValue(os.str());
    }

    InternalValue operator()(int64_t val) const
    {
        std::ostringstream os;
        os << val;
        return InternalValue(os.str());
    }
//
//    template<typename U>
//    InternalValue operator()(U&& val) const
//    {
//        return InternalValue();
//    }

    const RenderContext* m_context;
};

PrettyPrint::PrettyPrint(FilterParams params)
{
}

InternalValue PrettyPrint::Filter(const InternalValue& baseVal, RenderContext& context)
{
    return Apply<PrettyPrinter>(baseVal, &context);
}

Random::Random(FilterParams params)
{

}

InternalValue Random::Filter(const InternalValue& baseVal, RenderContext& context)
{
    return InternalValue();
}

SequenceAccessor::SequenceAccessor(FilterParams params, SequenceAccessor::Mode mode)
    : m_mode(mode)
{
    switch (mode)
    {
    case FirstItemMode:
        break;
    case LastItemMode:
        break;
    case LengthMode:
        break;
    case MaxItemMode:
        ParseParams({{"case_sensitive", false, InternalValue(false)}, {"attribute", false}}, params);
        break;
    case MinItemMode:
        ParseParams({{"case_sensitive", false, InternalValue(false)}, {"attribute", false}}, params);
        break;
    case ReverseMode:
        break;
    case SumItemsMode:
        ParseParams({{"attribute", false}, {"start", false}}, params);
        break;
    case UniqueItemsMode:
        ParseParams({{"attribute", false}}, params);
        break;
    }
}

InternalValue SequenceAccessor::Filter(const InternalValue& baseVal, RenderContext& context)
{
    InternalValue result;

    bool isConverted = false;
    ListAdapter list = ConvertToList(baseVal, isConverted);

    if (!isConverted)
        return result;

    InternalValue attrName = GetArgumentValue("attribute", context);
    InternalValue isCsVal = GetArgumentValue("case_sensitive", context, InternalValue(false));

    BinaryExpression::CompareType compType =
            ConvertToBool(isCsVal) ? BinaryExpression::CaseSensitive : BinaryExpression::CaseInsensitive;

    auto lessComparator = [&attrName, &compType](auto& val1, auto& val2) {
        InternalValue cmpRes;

        if (IsEmpty(attrName))
            cmpRes = Apply2<visitors::BinaryMathOperation>(val1, val2, BinaryExpression::LogicalLt, compType);
        else
            cmpRes = Apply2<visitors::BinaryMathOperation>(Subscript(val1, attrName), Subscript(val2, attrName), BinaryExpression::LogicalLt, compType);

        return ConvertToBool(cmpRes);
    };

    auto equalComparator = [&attrName, &compType](auto& val1, auto& val2) {
        InternalValue cmpRes;

        if (IsEmpty(attrName))
            cmpRes = Apply2<visitors::BinaryMathOperation>(val1, val2, BinaryExpression::LogicalEq, compType);
        else
            cmpRes = Apply2<visitors::BinaryMathOperation>(Subscript(val1, attrName), Subscript(val2, attrName), BinaryExpression::LogicalEq, compType);

        return ConvertToBool(cmpRes);
    };

    switch (m_mode)
    {
    case FirstItemMode:
        result = list.GetSize() == 0 ? InternalValue() : list.GetValueByIndex(0);
        break;
    case LastItemMode:
        result = list.GetSize() == 0 ? InternalValue() : list.GetValueByIndex(list.GetSize() - 1);
        break;
    case LengthMode:
        result = static_cast<int64_t>(list.GetSize());
        break;
    case MaxItemMode:
    {
        auto b = list.begin();
        auto e = list.end();
        auto p = std::max_element(b, e, lessComparator);
        result = p != e ? *p : InternalValue();
        break;
    }
    case MinItemMode:
    {
        auto b = list.begin();
        auto e = list.end();
        auto p = std::min_element(b, e, lessComparator);
        result = p != e ? *p : InternalValue();
        break;
    }
    case ReverseMode:
    {
        InternalValueList resultList(list.GetSize());
        for (int n = 0; n < list.GetSize(); ++ n)
            resultList[list.GetSize() - n - 1] = list.GetValueByIndex(n);

        result = ListAdapter::CreateAdapter(std::move(resultList));
        break;
    }
    case SumItemsMode:
    {
        ListAdapter l1;
        ListAdapter* actualList;
        if (IsEmpty(attrName))
        {
            actualList = &list;
        }
        else
        {
            l1 = list.ToSubscriptedList(attrName, true);
            actualList = &l1;
        }
        InternalValue start = GetArgumentValue("start", context);
        InternalValue resultVal = std::accumulate(actualList->begin(), actualList->end(), start, [](const InternalValue& cur, const InternalValue& val) {
            if (IsEmpty(cur))
                return val;

            return Apply2<visitors::BinaryMathOperation>(cur, val, BinaryExpression::Plus);
        });

        result = std::move(resultVal);
        break;
    }
    case UniqueItemsMode:
    {
        InternalValueList resultList;

        struct Item
        {
            InternalValue val;
            int64_t idx;
        };
        std::vector<Item> items;

        int idx = 0;
        for (auto& v : list)
            items.push_back(std::move(Item{IsEmpty(attrName) ? v : Subscript(v, attrName), idx ++}));

        std::sort(items.begin(), items.end(), [&compType](auto& i1, auto& i2) {
            auto cmpRes = Apply2<visitors::BinaryMathOperation>(i1.val, i2.val, BinaryExpression::LogicalLt, compType);

            return ConvertToBool(cmpRes);
        });

        auto end = std::unique(items.begin(), items.end(), [&compType](auto& i1, auto& i2) {
            auto cmpRes = Apply2<visitors::BinaryMathOperation>(i1.val, i2.val, BinaryExpression::LogicalEq, compType);

            return ConvertToBool(cmpRes);
        });
        items.erase(end, items.end());

        std::sort(items.begin(), items.end(), [&compType](auto& i1, auto& i2) {
            return i1.idx < i2.idx;
        });

        for (auto& i : items)
            resultList.push_back(list.GetValueByIndex(i.idx));

        result = ListAdapter::CreateAdapter(std::move(resultList));
        break;
    }
    }

    return result;
}

Serialize::Serialize(FilterParams params, Serialize::Mode mode)
{

}

InternalValue Serialize::Filter(const InternalValue& baseVal, RenderContext& context)
{
    return InternalValue();
}

Slice::Slice(FilterParams params, Slice::Mode mode)
{

}

InternalValue Slice::Filter(const InternalValue& baseVal, RenderContext& context)
{
    return InternalValue();
}

StringConverter::StringConverter(FilterParams params, StringConverter::Mode mode)
{

}

InternalValue StringConverter::Filter(const InternalValue& baseVal, RenderContext& context)
{
    return InternalValue();
}

StringFormat::StringFormat(FilterParams params, StringFormat::Mode mode)
{

}

InternalValue StringFormat::Filter(const InternalValue& baseVal, RenderContext& context)
{
    return InternalValue();
}

Tester::Tester(FilterParams params, Tester::Mode mode)
    : m_mode(mode)
{
    FilterParams newParams;

    if ((mode == RejectMode || mode == SelectMode) && params.kwParams.empty() && params.posParams.empty())
    {
        m_noParams = true;
        return;
    }

    if (mode == RejectMode || mode == SelectMode)
        ParseParams({{"tester", false}}, params);
    else
        ParseParams({{"attribute", true}, {"tester", false}}, params);

    m_testingParams.kwParams = std::move(m_args.extraKwArgs);
    m_testingParams.posParams = std::move(m_args.extraPosArgs);
}

InternalValue Tester::Filter(const InternalValue& baseVal, RenderContext& context)
{
    InternalValue testerName = GetArgumentValue("tester", context);
    InternalValue attrName = GetArgumentValue("attribute", context);

    TesterPtr tester;

    if (!IsEmpty(testerName))
    {
        tester = CreateTester(AsString(testerName), m_testingParams);

        if (!tester)
            return InternalValue();
    }

    bool isConverted = false;
    auto list = ConvertToList(baseVal, isConverted);
    if (!isConverted)
        return InternalValue();

    InternalValueList resultList;
    resultList.reserve(list.GetSize());
    std::copy_if(list.begin(), list.end(), std::back_inserter(resultList), [this, tester, attrName, &context](auto& val)
    {
        InternalValue attrVal;
        bool isAttr = !IsEmpty(attrName);
        if (isAttr)
            attrVal = Subscript(val, attrName);

        bool result = false;
        if (tester)
            result = tester->Test(isAttr ? attrVal : val, context);
        else
            result = ConvertToBool(isAttr ? attrVal : val);

        return (m_mode == SelectMode || m_mode == SelectAttrMode) ? result : !result;
    });

    return ListAdapter::CreateAdapter(std::move(resultList));
}

ValueConverter::ValueConverter(FilterParams params, ValueConverter::Mode mode)
{

}

InternalValue ValueConverter::Filter(const InternalValue& baseVal, RenderContext& context)
{
    return InternalValue();
}


} // filters
} // jinja2
