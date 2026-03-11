#include "tools_common.h"
#include <terark/json.hpp>
#include <terark/util/mmap.hpp>
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
namespace topling {
// ref to libruledb.so
std::shared_ptr<std::string> get_field_data(json&);
bool is_regex(const json& jval);
}
enum JsonType { kNotJson = 0, kParseJson = 1, kMatchJson = 2 };
int main(int argc, char** argv) {
    bool treat_unknown_fields_as_content = false;
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
            treat_unknown_fields_as_content = true;
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
    if (optind >= argc) {
        HIGH_LIGHT("ERROR: Missing InputFile\n");
        usage(argv[0]);
        return 2;
    }
    const char* finput_name = argv[optind];
    MmapWholeFile fmmap;
    try {
        MmapWholeFile(finput_name).swap(fmmap);
    } catch (const std::exception& ex) {
        HIGH_LIGHT("FATAL: mmap file(%s) = %s\n", finput_name, ex.what());
        return 1;
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
    matcher.treat_unknown_fields_as_content(treat_unknown_fields_as_content);
    matcher.regex_start_at_word(start_at_word);
    profiling pf;
    long long t0 = pf.now();
    long long len_matched = 0, len_missed = 0;
    long long sum_matched_rules = 0;
    long long sum_candidate_rules = 0;
    int num_matched = 0, num_missed = 0;
  for (int rpt = 0; rpt < repeat; rpt++) {
    for (size_t line_iter = 0; line_iter < fmmap.size; ) {
        fstring line = fmmap.memory().iter_field(line_iter, '\n').chomp();
        lineno++;
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
            for(size_t i = 0, field_iter = 0; field_iter < line.size(); i++) {
                auto fv = line.iter_field(field_iter, '\t').trim();
                if (fv.empty())
                    continue;
                intptr_t name_end = 0;
                for (; name_end < fv.n; name_end++) {
                    byte_t ch = fv[name_end];
                    if (isalnum(ch) || '.' == ch || '_' == ch || '-' == ch) {
                        //
                    } else if (':' == ch) {
                        break;
                    } else {
                        name_end = fv.n;
                        break;
                    }
                }
                if (name_end < fv.n && !isdigit(fv.uch(0))) {
                    fstring name = fv.prefix(name_end).trim();
                    fstring value = fv.substr(name_end+1).trim();
                    DEBUG(4, "F[%zd]: name: %.*s, value: %.*s", i, name.ilen(), name, value.ilen(), value);
                    doc[name.str()] = { make_shared<string>(value.str()),
                                        value.size() && strchr("({", value[0]) };
                }
                else {
                    DEBUG(4, "F[%zd] missing fieldname, treating it as a general field: %s", i, fv);
                    doc["zth"+to_string(i)].text = make_shared<string>(fv.str());
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
                    auto detail = matcher.get_match_pos_view(rule_id);
                    printf("  rule %d with fields %zd\n", rule_id, detail.size());
                    for (auto& [field, vec] : detail) {
                        printf("    field \"%s\" with %zd hits:", field.data(), vec.size());
                        auto iter = doc.find(std::string(field));
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
        if (be_quiet && show_pos) {
            fflush(stdout);
        }
        sum_candidate_rules += matcher.total_candidates();
    }
  } // repeat
  if (!(be_quiet && show_pos)) {
    long long t1 = pf.now();
    printf("time %8.3f sec, valid docs %d matched %lld rules of candidates %lld(%.3f%%), matched %d %.3f MB, missed %d %.3f MB, %.3f MB/sec\n",
        pf.sf(t0,t1), num_matched + num_missed, sum_matched_rules,
        sum_candidate_rules, 100.0*sum_matched_rules/sum_candidate_rules,
        num_matched, len_matched / 1e6,
        num_missed , len_missed  / 1e6,
        (len_matched + len_missed) / pf.uf(t0,t1)
    );
    long long totaldoc = num_matched + num_missed;
    printf("\n");
    printf("perf        |   num   | latency(us) | operations/sec\n");
    printf("-----------:|--------:|---------:|---------------:\n");
    printf("document    | %7lld | %8.3f | %8.1f\n", totaldoc, pf.uf(t0,t1)/totaldoc, totaldoc/pf.sf(t0,t1));
    printf("matched rule| %7lld | %8.3f | %8.1f\n", sum_matched_rules, pf.uf(t0,t1)/sum_matched_rules, sum_matched_rules/pf.sf(t0,t1));
    printf("candite rule| %7lld | %8.3f | %8.1f\n", sum_candidate_rules, pf.uf(t0,t1)/sum_candidate_rules, sum_candidate_rules/pf.sf(t0,t1));
    printf("\n");
  }
    return 0;
}
