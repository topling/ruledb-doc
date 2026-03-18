# <a name="ruledbjava"></a> RuleDB Java API

## RuleDB
RuleDB 对象是 `AutoCloseable` 的，包含以下方法：
```java
package cn.topling.ruledb;
class RuleDB implements AutoCloseable {
    public static RuleDB open(String dbdir);
    public void hotSwap(String dbdir);
    public RuleMatcher createRuleMatcher() throws Exception;

    // stdfiledata.length should be 3,
    //            [0] is stdin, not used now, [1] is stdout, [2] is stderr
    public native static void runCompiler(String[] args, String[] stdfiledata);
}
```

java RuleDB open 时自动将 UserData 存储为字符串，`hotSwap` 也会自动处理 String 类型的 UserData 。

JNI 不管 UserData 的内容细节，业务代码应当自己解析，或使用快捷方式 `getUserLongId`（这适用于大多数场景）。

## RuleMatcher

```java
package cn.topling.ruledb;
public class RuleMatcher implements AutoCloseable {
    public int[] match(Map<String, String> doc);
    public int[] matchComplex(Map<String, ComplexQuery> doc);
    public int[] matchJson(byte[] json, int offset, int len);
    public int[] matchJson(byte[] json);
    public Map<String, MatchPosInfo[]> getMatchPos(int ruleId);

    // options
    public void treatUnknownFieldsAsContent(boolean b);
    public boolean treatUnknownFieldsAsContent();
    public void regexStartAtWord(boolean b);
    public boolean regexStartAtWord();

    public int totalCandidates(); // 非必要，仅供统计、日志等
    public String getUserData(int rule_id); // 必要，关联业务数据，复杂业务
    public long getUserLongId(int rule_id); // 最简单的业务数据类型：id
}
```

其中 ComplexQuery 和 MatchPosInfo：
```java
public class ComplexQuery {
    public ComplexQuery(String data, boolean is_regex);
    public String data();
    public boolean is_regex();
}

public class MatchPosInfo {
    public enum MatchType { tExact, tRegex, tFullMatch };
    public MatchPosInfo(MatchType matchType, int matchId, int pos, int len);
    public MatchType matchType();
    public int matchId();
    public int pos();
    public int len();
    public int endpos();
}
```

## 用法

```java
    RuleDB db = RuleDB.open(dbdir);
    RuleMatcher matcher = db.createRuleMatcher();
    byte[] jsonStr = fromHttp(...);
    int [] matchset = matcher.matchJson(jsonStr);
    for (int ruleId : matchset) {
        // getUserLongId 自动识别 10/16/8进制，需注意 "0123" 会解释为 8 进制
        long userCategoryId = matcher.getUserLongId(ruleId); // 或复杂情况：
        // userCategoryId 只是业务数据 UserData 的一部分
        // long userCategoryId = parseId(matcher.getUserData(ruleId));
        System.out.printf("match category %s%n", userCategoryId);
        if (shouldShowMatchPos(userCategoryId)) {
            Map<String, MatchPosInfo[]> detail = matcher.getMatchPos(ruleId);
            for (Map.Entry<String, MatchPosInfo[]> kv : detail.entrySet()) {
                String fieldname = kv.getKey();
                MatchPosInfo[] vec = kv.getValue();
                System.out.printf("    fieldname \"%s\" with %d hits:", fieldname, vec.length);
                for (MatchPosInfo hit : vec) {
                    System.out.printf(" [%d %d] %s-%d,", hit.pos(), hit.len(),
                        hit.matchType().name(), hit.matchId());
                }
                System.out.println("");
            }
        }
    }
```

虽然 RuleDB 比同类竞品快两个数量级以上，但仍应注意避免不必要的开销：
例如 `getMatchPos()` 有一定开销，使用 `shouldShowMatchPos()` 判断条件以便按需调用 `getMatchPos()`

## 热更新/替换

```java
    db.hotSwap("new/db/dir");
```
或者，在 Linux 下可以**保持 dbdir 不变**，然后：

1. 删除原 dbdir, 例如为简单起见用外部脚本删除，**绝不能直接覆盖**
   * 工业应用中注意备份旧数据
2. 编译新规则库: 调用 rule_db_build.sh 或 java RuleDB.runCompiler
3. 最后调用 hotSwap:

```java
    db.hotSwap(dbdir); // 仍是 db.open(dbdir) 中的 dbdir
```

hotSwap 具有事务属性，失败会抛出异常，相当于啥也没发生。

hotSwap 之后，原有的 RuleMatcher 对象仍引用的是之前的 RuleDB 底层对象/数据。新的 RuleMatcher 对象将引用新的 RuleDB 底层对象/数据。
