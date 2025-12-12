# 地理位置查询

跟[整数字段](integer-fields.md)类似，规则数据库对地理位置表达式没有直接的支持。可以使用**地理空间哈希（Geohash）** 和正则表达式，把对地理位置的限定转化为 $\text{Geohash}$ 限定，将地理位置查询转化为规则引擎的高性能字符串匹配。

## 核心原理：Geohash 编码

$\text{Geohash}$ 是一种将二维的经纬度坐标编码为一个**一维字符串**的算法，它具有**前缀特性（Prefix Property）**：

* **前缀越长，网格区域越小，精度越高。**
* **拥有相同前缀的 $\text{Geohash}$ 码，在地理位置上是邻近的。**

我们利用 $\text{Geohash}$ 的前缀特性，通过**正则前缀匹配**来实现 **“点在区域内”** 的查询，
这种查询必须**完全匹配**字段的所有内容，所以只能使用fieldname{{}} 或 fieldname[]，不能使用 fieldname(...)。

## 规则定义：简单区域限定

假设我们需要限定一个用户在某一特定区域（例如， $\text{Geohash}$ 码以 `u4pruy` 开头的 $0.6\text{km} \times 0.6\text{km}$ 区域内）：

### 1. 规则（单区域包含）

我们定义一个伪字段 `user_location`，其内容为用户的 $\text{Geohash}$ 码。

```
# 匹配以 u4pruy 开头的所有 geohash
user_location{{u4pruy[0-9a-z]*}}
```

### 2. 规则（多区域 OR 关系）

如果一个规则需要匹配多个不连续的区域，可以使用正则的 $\text{OR}$ 语法：

```
# 匹配三个不连续的区域
user_location{{(u4pruy|u4prqg|u4p1k2)[0-9a-z]*}}
```

## 复合查询：区域的并、交、差运算

对于复杂的区域限定，例如“匹配区域 $\text{A}$，但排除区域 $\text{B}$”（区域 $\text{A}$ 减去区域 $\text{B}$），我们可以使用规则数据库支持的 $\text{[ ]}$ **复合正则表达式**进行集合运算。

### 1. 案例：区域差集 (A AND NOT B)

**需求：** 匹配 NYC 的 $\text{dr5r}$ 区域（ $4.9\text{km} \times 4.9\text{km}$），但排除其中的中央公园 $\text{dr5ru}$ 区域（ $1.2\text{km} \times 0.6\text{km}$ ）。

**规则定义：**
```
user_location[ {{dr5r[0-9a-z]*}} - {{dr5ru[0-9a-z]*}} ]
```
* `{{dr5r[0-9a-z]*}}`：所有位于大区域 $\text{A}$ 内的 $\text{子区域}$。
* `{{dr5ru[0-9a-z]*}}`：所有位于子区域 $\text{B}$ 内的 $\text{子区域}$。
* `[ A - B ]`：计算差集，实现精确的区域排除。

### 2. 案例：区域限定与其它限定混合
**需求：** 匹配区域 $\text{A}$ 和区域 $\text{B}$ 的共同交集，并且性别为男，年龄 20 到 25 岁，收入 8000 到 10000。
> RuleDB 支持[联合索引](https://github.com/topling/ruledb-doc/blob/main/realnum.md#%E4%BE%8B%E7%BB%8F%E7%BA%AC%E5%BA%A6)，此处仅作为原理性展示

提示：参考[整数字段](integer-fields.md) 限定。

**规则定义：**
```
gender_age_income_location[ \
    {{1[\i{20}-\i{25}][\i{8000}-\i{10000}]}} . \
    ( {{dr5r[0-9a-z]*}} - {{dr5ru[0-9a-z]*}} ) \
]
```

其中 `\` 是续行符，`.` 操作符是复合正则表达式的 **连接** 操作。**连接** 的含义实际上就是逐个检查各个条件，都满足时，（表示文档/数据项的）字符串匹配成功。

## 排除非法的 geohash
前述 geohash 包含 `[0-9a-z]` 的所有字符，但实际上 geohash 不包含 `[ailo]`，如果使用标准正则表达式来描述，是这样的：

```perl
[0-9b-hj-kmnp-z]*
```
因为缺乏 `排除` 语法，不够直观，使用 Topling 复合正则表达式，就可以这样写：
```perl
{{[0-9a-z]*}} - {{.*[ailo].*}}
```
或者这样：
```perl
({{[0-9a-z]}} - {{[ailo]}})+
```

这样，前面最复杂的混合了整数字段限定的规则就可以写为：
```perl
gender_age_income_location[ \
    {{1[\i{20}-\i{25}][\i{8000}-\i{10000}]}} . \
    (( {{dr5r[0-9a-z]*}} - {{dr5ru[0-9a-z]*}} ) - {{.*[ailo].*}}) \
]
```

值得一提的是：对于 `A - B` 这样的复合正则表达式，`B` 不必是 `A` 的子集更不必`比A小`，它的运算结果是 `能匹配 A 但不能匹配 B` 的所有字符串集合。

再次强调：所有这些复杂的正则表达式运算都发生在编译时，不是运行时，对匹配性能没有任何影响，并且无论写法如何，只要语义相同，最终编译出来的 DFA 是完全相同的。

## 创建规则库
需要用 -F fieldname 定义字段
```
rule_db_build.sh -F user_location \
                 -F gender_age_income_location \
                 -o dbdir rule.txt
```

## 查询匹配的规则

在查询规则库之前，用户 $\text{doc}$ 必须将经纬度坐标编码为 $\text{Geohash}$ 字符串，并添加到 $\text{doc}$ 的伪字段中。

由于 $\text{Geohash}$ 码是标准 $\text{ASCII}$ 字符串，您可以直接进行编码和添加：

```c++
// 假设用户位置的 Geohash 码是 "dr5rbp"
map<string, shared_ptr<string> > doc;
// 实际生产中，需要根据查询规则的要求确定 Geohash 的长度
shared_ptr<string> geohash_code = make_shared<string>("dr5rbp");
// doc["user_location"] = geohash_code; // 应优先精确条件
doc["gender_age_income_location"] = gender_age_income_location;
matcher.match(doc);
```