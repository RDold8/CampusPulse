# 东北电力大学来源核查

核查日期：2026-10-02。当前学校配置已启用六个栏目，覆盖校本部、教务处、学工在线和团委四个站点。新增布局有离线列表/详情样本，学工配置有限翻页到第二页；程序实网验证记录见evidence/multi-source-proof.json。就业网动态列表仍未启用，研究生院未接入。

| 来源 | 已验证入口 | 当前范围 |
|---|---|---|
| 学校官网通知 | https://www.neepu.edu.cn/cs1.htm | 首页15条，含校园活动；正文.v_news_content |
| 教务处通知 | https://jwc.neepu.edu.cn/tzgg.htm | 首页20条，含考试、竞赛、教务；缴费详情与附件已验证 |
| 教务处考试重修 | https://jwc.neepu.edu.cn/kszx.htm | 首页20条，保留官方名单附件链接 |
| 学工在线通知通告 | https://xsc.neepu.edu.cn/list.jsp?urltype=tree.TreeTempUrl&wbtreeid=1002 | 前两页各10条，第二页包含优秀学生奖学金结果公示；不同于导航里另两个“通知公告”入口 |
| 团委通知 | https://54shine.neepu.edu.cn/list.jsp?urltype=tree.TreeTempUrl&wbtreeid=1085 | 首页5条；读取完整标题并剔除标题内嵌的日期 |
| 团学动态 | https://54shine.neepu.edu.cn/list.jsp?urltype=tree.TreeTempUrl&wbtreeid=1090 | 单独栏目配置，校园活动与团学报道 |
| 就业信息网 | https://jy.neepu.edu.cn/ | 首页可读；动态列表需要公开接口适配，直接请求已发现接口返回400，未接入 |
| 研究生院 | https://grad.neepu.edu.cn/ | 发现状态，未接入 |

## 已发现的实际边界

1. [教务处通知列表](https://jwc.neepu.edu.cn/tzgg/41.htm)同时出现学生重修报名、竞赛安排和考试考务培训；[另一个列表](https://jwc.neepu.edu.cn/tzgg/39.htm)包含监考教师名单报送。不能将教务处所有“考试”相关通知统一推给学生。
2. [学工在线优秀学生奖学金公示](https://xsc.neepu.edu.cn/info/1002/18069.htm)有公示区间和 XLSX 名单附件。它是结果公示，而非申请通知；可提取公示区间，名单不进入面向所有用户的摘要。
3. [团委官网](https://54shine.neepu.edu.cn/index.jsp)检索可见通知公告，说明校园活动可能需要独立来源，不能只扫学校首页。
4. [就业站招聘须知](https://jy.neepu.edu.cn/detail/news?id=656522&menu_id=36790)说明招聘信息与线下专场宣讲属于不同业务入口，应分开建 source 或内容类型。
5. [就业站一则招聘会详情](https://jy.neepu.edu.cn/detail/jobfair?id=27246)的正文实际描述西南石油大学双选会。它证明“在东电站点发布”与“在东电举办”不能混用；保存 source_school、host_school 和 location，转载信息要有可解释标记。该样本不作为当前可参加活动推荐。

## 首批接入选择

第一批先接教务处通知及考试重修栏目，验证重修报名/缴费/名单/课表的跨通知办事链；当前已扩展学工和团委列表；学工样本为奖学金结果公示，申请材料和院系截止仍需独立实例验证；就业来源后续扩展。学院站按个人订阅需求扩展，须分别核实学院上报期限和学生截止时间。

[历史重修缴费通知](https://jwc.neepu.edu.cn/info/1014/5548.htm)是本次核心样本，明确缴费名单与有效名单的关系；[历史报名通知](https://jwc.neepu.edu.cn/info/1014/7198.htm)则说明网上报名是另一操作。两者属于不同学期，不作为同一 Case 合并；可分别测试提取器。所有历史样本默认不发送当前提醒。

所有来源进入可运行状态前，应保存带抓取日期的离线测试样本，确认字符编码、列表发布日期、原文 URL、正文、附件和下一页；用官方入口确认域名和栏目，不以搜索结果猜 CSS 选择器或 API。

## 未确认事项

- 教务处robots.txt实查返回404，未发现该入口的规则文件；这不等于站点提供了无限制采集许可。当前低频读取已启用来源的有限页数并按需读取正文，客户端串行、至少3秒间隔。持续公共服务上线仍需核对来源策略。
- 10月2日直接HTTP读取确认了此前部分超时站点的可访问性；就业接口400的原因仍未确定，不能仅据此称为反爬。
- 日期模型和 ICS 依据 [RFC 5545](https://www.rfc-editor.org/rfc/rfc5545)设计；手机连接依据 [Apple](https://support.apple.com/en-euro/guide/iphone/iph3d1110d4/ios)和 [Google](https://support.google.com/calendar/answer/37100)官方路径，实际设备与账号尚未验证。
