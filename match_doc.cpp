#include "tools_common.h"
#include <terark/json.hpp>
void usage(const char* prog) {
    fprintf(stderr, R"EOS(Usage:
   %s Options InputTabSeperateDocFile

Description:
   Match each input line as a doc which fields are tab seperated

Options:
   -h : Show this help infomation
   -d dbdir
   -I : ignore unknown fields(default true)
   -j : parse each line as a json
   -q : Be quiet, don't print progress info
   -w : match regex start at word for scan speed(default false)

)EOS"
        , prog);
}
static std::shared_ptr<std::string>
get_field_data(json& jval) {
    using namespace std;
    if (jval.is_object())
        return make_shared<string>(std::move(jval.at("text").get_ref<string&>()));
    else
        return make_shared<string>(std::move(jval.get_ref<string&>()));
}
static bool is_regex(const json& jval) {
    if (jval.is_object())
        return jval.at("is_regex").get<bool>();
    else
        return false;
}
enum JsonType { kNotJson = 0, kParseJson = 1, kMatchJson = 2 };
int main(int argc, char** argv) {
    bool ignore_unknow_fields = false;
    JsonType json_type = kNotJson;
    bool be_quiet = false;
    bool show_pos = false;
    bool start_at_word = false;
    int repeat = 1;
    string dbdir;
    for (;;) {
        int opt = getopt(argc, argv, "d:hpqr:wIj::");
        switch (opt) {
        default:
        case 'h':
        case '?':
            usage(argv[0]);
            return 2;
        case -1:
            goto GetoptDone;
            break;
        case 'd':
            dbdir = optarg;
            break;
        case 'I':
            ignore_unknow_fields = true;
            break;
        case 'j':
            json_type = optarg ? (JsonType)atoi(optarg) : kParseJson;
            break;
        case 'p':
            show_pos = true;
            break;
        case 'q':
            be_quiet = true;
            break;
        case 'r':
            repeat = std::max(atoi(optarg), 1);
            break;
        case 'w':
            start_at_word = true;
            break;
        }
    }
GetoptDone:
    if (dbdir.empty()) {
        HIGH_LIGHT("ERROR: Missing -d dbdir\n");
        usage(argv[0]);
        return 2;
    }
    Auto_close_fp finput;
    if (optind < argc) {
        const char* finput_name = argv[optind];
        finput = fopen(finput_name, "r");
        if (NULL == finput) {
            HIGH_LIGHT("FATAL: fopen(%s, r) = %s\n", finput_name, strerror(errno));
            return 3;
        }
    }
    else if (g_debug >= 1) {
        HIGH_LIGHT("Reading from stdin...");
    }
    RuleDatabase db;
    if (!db.open(dbdir)) {
        HIGH_LIGHT("FATAL: db.open(%s) = %s\n", dbdir.c_str(), db.strerr());
        return 1;
    }
    RuleMatcher matcher;
    if (!matcher.init(db)) {
        HIGH_LIGHT("FATAL: matcher.init(%s) = %s\n", dbdir.c_str(), matcher.strerr());
        return 1;
    }
    matcher.ignore_unknown_fields(ignore_unknow_fields);
    matcher.regex_start_at_word(start_at_word);
    LineBuf line;
    valvec<fstring> F; // fields
    profiling pf;
    long long t0 = pf.now();
    long long len_matched = 0, len_missed = 0;
    long long sum_matched_rules = 0;
    long long sum_candidate_rules = 0;
    int num_matched = 0, num_missed = 0;
  for (int rpt = 0; rpt < repeat; rpt++) {
    rewind(finput.self_or(stdin));
    while (line.getline(finput.self_or(stdin)) > 0) {
        lineno++;
        line.chomp();
        map<string, RuleMatcher::ComplexQuery> doc;
        if (kParseJson == json_type) try {
            json js = json::parse(line.begin(), line.end());
            for (auto iter : js.items()) {
                if (iter.value().is_structured()) {
                    ERROR("field %s: value must be string or primitive", iter.key());
                    continue;
                }
                auto value = make_shared<string>(iter.value().get_ref<string&>());
                if (!value->empty()) {
                    doc[iter.key()] = {value, !!strchr("({", value->at(0))};
                }
            }
        } catch (const std::exception& ex) {
            ERROR("json::parse() = %s", ex.what());
            continue;
        }
        else if (kMatchJson == json_type) {
            if (!matcher.match(line.begin(), line.size())) {
                ERROR("matcher.match(json) = %s", matcher.strerr());
                continue;
            }
        } else {
            line.split('\t', &F);
            for (size_t i = 0; i < F.size(); i++) {
                int name_end = -1;
                char mark;
                int num = sscanf(F[i].c_str(), "%*[a-zA-Z0-9_-]%n:%c", &name_end, &mark);
                if (num == 1 && !isdigit(F[i].uch(0))) {
                    fstring name = F[i].prefix(name_end).trim();
                    fstring value = F[i].substr(name_end+1).trim();
                    DEBUG(4, "F[%zd]: name: %.*s, value: %.*s", i, name.ilen(), name, value.ilen(), value);
                    doc[name.str()] = { make_shared<string>(value.str()),
                                        value.size() && strchr("({", value[0]) };
                }
                else {
                    DEBUG(4, "F[%zd] missing fieldname, treating it as a general field: %s", i, F[i]);
                    fstring fieldvalue = F[i].trim();
                    doc["zth"+to_string(i)].text = make_shared<string>(fieldvalue.str());
                }
            }
        }
        if (kMatchJson != json_type && !matcher.match(doc)) {
            ERROR("matcher.match(doc) = %s", matcher.strerr());
            continue;
        }
        if (auto& matchset = matcher.get_result(); matchset.empty()) {
            if (!be_quiet)
                printf("line %ld: no match\n", lineno);
            num_missed++;
            len_missed += line.size();
        } else {
            if (!be_quiet || show_pos) {
                printf("line %ld:", lineno);
                for (int rule_id : matchset)
                    printf(" %d", rule_id);
                printf("\n");
            }
            if (show_pos) {
                if (kMatchJson == json_type) {
                    // re-parse, because match(json) does not expose json object
                    json js = json::parse(line.begin(), line.end());
                    for (auto& [key, val] : js.items())
                        doc[key] = {get_field_data(val), is_regex(val)};
                }
                for (int rule_id : matchset) {
                    auto detail = matcher.get_match_pos(rule_id);
                    printf("  rule %d with fields %zd\n", rule_id, detail.size());
                    for (auto& [field, vec] : detail) {
                        printf("    field \"%s\" with %zd hits:", field.c_str(), vec.size());
                        auto iter = doc.find(field);
                        if (iter == doc.end()) {
                            printf(" composite index full match\n");
                            continue;
                        }
                        auto fv = iter->second.text->data();
                        for (auto& hit : vec) {
                            printf(" [%d %d]{%.*s} %s-%d,", hit.pos, hit.len,
                                hit.len, fv + hit.pos, hit.match_type_name(), hit.match_id);
                        }
                        printf("\n");
                    }
                }
            }
            num_matched++;
            len_matched += line.size();
            sum_matched_rules += matchset.size();
        }
        sum_candidate_rules += matcher.total_candidates();
    }
  } // repeat
    long long t1 = pf.now();
    printf("time %8.3f sec, valid docs %d matched %lld rules of candidates %lld(%.3f%%), matched %d %.3f MB, missed %d %.3f MB, %.3f MB/sec\n",
        pf.sf(t0,t1), num_matched + num_missed, sum_matched_rules,
        sum_candidate_rules, 100.0*sum_matched_rules/sum_candidate_rules,
        num_matched, len_matched / 1e6,
        num_missed , len_missed  / 1e6,
        (len_matched + len_missed) / pf.uf(t0,t1)
    );
    return 0;
}