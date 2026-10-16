#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include "storage_policy.h"
#include <qrcode.h>
#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "EPaper_213_Driver.h"
#include "gui.h"
#include "icons.h"

#define TRUE 1
#define FALSE 0

static const uint8_t COLOR_BLACK = 0;
static const uint8_t COLOR_LIGHT = 2;
static const uint8_t COLOR_WHITE = 3;

static const uint8_t PCF8563_ADDR = 0x51;
static const uint8_t I2C_SDA_PIN = 21;
static const uint8_t I2C_SCL_PIN = 22;

static const uint8_t BUTTON_NEXT_PIN = 32;
static const uint8_t BUTTON_PREV_PIN = 25;
static const uint8_t BUTTON_OK_PIN = 33;
static const uint8_t BUTTON_BACK_PIN = 26;
static const uint8_t MOTOR_PIN = 27;
static const uint8_t BATTERY_ADC_PIN = 34;
static const uint8_t CHARGE_ACTIVE_PIN = 35;
static const uint8_t CHARGE_DONE_PIN = 39;
static const uint8_t BUTTON_NEXT_MASK = 1 << 0;
static const uint8_t BUTTON_PREV_MASK = 1 << 1;
static const uint8_t BUTTON_OK_MASK = 1 << 2;
static const uint8_t BUTTON_BACK_MASK = 1 << 3;
static const uint8_t BUTTON_ALL_MASK =
    BUTTON_NEXT_MASK | BUTTON_PREV_MASK | BUTTON_OK_MASK | BUTTON_BACK_MASK;
static const uint32_t BUTTON_RELEASE_STABLE_MS = 60;
static const uint32_t CLOCK_REFRESH_MS = 10000;
static const uint8_t LOW_BATTERY_ALERT_PERCENT = 15;
static const uint8_t LOW_BATTERY_RESET_PERCENT = 20;
static constexpr uint16_t BATTERY_DIVIDER_TOP_KOHM = 220;
static constexpr uint16_t BATTERY_DIVIDER_BOTTOM_KOHM = 100;
static constexpr uint16_t BATTERY_ADC_CALIBRATION_PERMILLE = 1000;
static constexpr uint8_t BATTERY_ADC_SAMPLE_COUNT = 16;
static const char *AP_SSID = "FortunePager";
static const char *AP_PASS = "12345678";
static const char *OTA_USER = "admin";
static const char *FIRMWARE_VERSION = "2.8.0";
static const char *CUSTOM_IMAGE_PATH = "/custom.bw";
static const char *TODO_PATH = "/todos.txt";
static const char *BOOK_PATH = "/book.txt";
static const char *CUSTOM_FOOD_PATH = "/foods.txt";
static const char *CUSTOM_DRINK_PATH = "/drinks.txt";
static const char *VOCAB_CET6_PATH = "/vocab_cet6.tsv";
static const char *VOCAB_IELTS_PATH = "/vocab_ielts.tsv";
static const char *VOCAB_CET6_PROGRESS_PATH = "/vocab_cet6.dat";
static const char *VOCAB_IELTS_PROGRESS_PATH = "/vocab_ielts.dat";
static constexpr uint16_t CUSTOM_IMAGE_BYTES = (212 * 104) / 8;
static constexpr uint8_t TODO_MAX_ITEMS = 12;
static constexpr uint8_t BOOK_LINES_PER_PAGE = 5;
static constexpr uint8_t BOOK_LINE_UNITS = 32;
static constexpr uint32_t BOOK_CHECKPOINT_PAGES = 64;
static const char *BOOK_INDEX_PATH = "/book.idx";
static constexpr uint8_t CUSTOM_OPTION_MAX = 12;
static constexpr uint8_t CUSTOM_OPTION_UNITS = 20;
static constexpr uint32_t POMODORO_FOCUS_MS = 25UL * 60UL * 1000UL;
static constexpr uint32_t POMODORO_BREAK_MS = 5UL * 60UL * 1000UL;
static constexpr uint32_t POMODORO_REFRESH_MS = 60UL * 1000UL;
static constexpr uint8_t VOCAB_QUEUE_MAX = 96;
static constexpr uint16_t VOCAB_CHECKPOINT_WORDS = 64;
static constexpr uint8_t VOCAB_DEFAULT_DAILY_NEW = 10;
static constexpr uint8_t VOCAB_MIN_DAILY_NEW = 5;
static constexpr uint8_t VOCAB_MAX_DAILY_NEW = 30;

uint8_t Display_buffer[5618];

class Cz04Canvas : public Adafruit_GFX {
public:
  Cz04Canvas() : Adafruit_GFX(212, 104) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    DrawPixel((uint16_t)x, (uint16_t)y, color ? COLOR_WHITE : COLOR_BLACK, TRUE, 1);
  }
};

struct DateTimeValue {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

struct BatteryStatus {
  bool present;
  bool charging;
  bool chargeComplete;
  bool usbPowered;
  uint16_t millivolts;
  uint8_t percent;
};

struct Page {
  const char *title;
  const char *line1;
  const char *line2;
  const char *line3;
};

struct TarotCard {
  const char *name;
  const char *uprightMeaning;
  const char *uprightAdvice;
  const char *reversedMeaning;
  const char *reversedAdvice;
};

struct DiceResult {
  uint8_t minRoll;
  uint8_t maxRoll;
  const char *grade;
  const char *meaning;
  const char *advice;
};

struct DailyItem {
  const char *name;
  const char *line1;
  const char *line2;
};

struct RandomState {
  uint32_t dateKey;
  uint16_t value;
  bool ready;
};

struct TodoItem {
  bool done;
  String text;
};

struct VocabProgress {
  uint16_t dueDay;
  uint8_t stage;
  uint8_t reserved;
};
static_assert(sizeof(VocabProgress) == 4, "Vocab progress layout changed");

struct VocabStats {
  uint16_t total;
  uint16_t learned;
  uint16_t due;
};

enum VocabRating : uint8_t {
  VOCAB_FORGOT,
  VOCAB_FUZZY,
  VOCAB_KNOW
};

enum PomodoroMode : uint8_t {
  POMODORO_FOCUS,
  POMODORO_BREAK
};

static const Page pages[] = {
  {"赛博算命先生", "NEXT/PREV 翻页", "OK 进入今日运势", "BACK 熄屏"},
  {"时钟", "读取 PCF8563", "OK 进入设置", "不会每秒刷新"},
  {"今日运势", "吉凶一签", "OK 主动重抽", "NEXT 看塔罗"},
  {"塔罗牌", "每日单牌", "OK 主动重抽", "NEXT 看骰子"},
  {"今天吃什么", "每日推荐", "OK 主动重抽", "NEXT 看喝什么"},
  {"今天喝什么", "每日推荐", "OK 主动重抽", "NEXT 看骰子"},
  {"六面骰子", "D6 随机点数", "OK 主动重掷", "每日结果会保存"},
  {"二十面骰子", "D20 成败判定", "OK 主动重掷", "每日结果会保存"},
  {"抛硬币", "正面或反面", "OK 再抛一次", "适合二选一"},
  {"随机数", "抽取 1 到 100", "OK 再抽一次", "硬件随机数"},
  {"今日关键词", "每天一个提示", "OK 主动重抽", "当天保持一致"},
  {"是非神谕", "给问题一个方向", "OK 再问一次", "不替代你的判断"},
  {"猜拳", "石头剪刀布", "OK 再出一次", "硬件随机选择"},
  {"今日宜忌", "一件宜做的事", "一件暂缓的事", "OK 主动重抽"},
  {"幸运坐标", "数字 方位 色彩", "每天自动生成", "OK 主动重抽"},
  {"我的图片", "显示网页上传图片", "断电仍然保留", "网页可随时替换"},
  {"设备状态", "固件与运行信息", "查看内存和连接", "OK 刷新"},
  {"YES / NO", "给问题一个明确答案", "OK 再问一次", "右侧显示结果"},
  {"番茄钟", "25分钟专注 / 5分钟休息", "OK 开始或暂停", "BACK 重置"},
  {"待办事项", "手机网页同步", "OK 完成下一项", "最多保存12项"},
  {"电子书", "手机上传TXT文本", "OK 进入阅读", "阅读中前后翻页"},
  {"喝水记录", "OK 记录喝了一杯", "手机设置杯量和目标", "每天自动归零"},
  {"心情指示器", "记录现在的心情", "OK 切换状态", "右侧显示表情"},
  {"三餐提醒", "按当前时间提示", "错过会提醒下次记得", "OK 刷新状态"},
  {"背单词", "六级或雅思词汇", "按记忆情况自动复习", "OK 开始今日学习"}
};

static constexpr uint8_t PAGE_CLOCK = 1;
static constexpr uint8_t PAGE_LUCK = 2;
static constexpr uint8_t PAGE_TAROT = 3;
static constexpr uint8_t PAGE_FOOD = 4;
static constexpr uint8_t PAGE_DRINK = 5;
static constexpr uint8_t PAGE_D6 = 6;
static constexpr uint8_t PAGE_D20 = 7;
static constexpr uint8_t PAGE_COIN = 8;
static constexpr uint8_t PAGE_RANDOM = 9;
static constexpr uint8_t PAGE_KEYWORD = 10;
static constexpr uint8_t PAGE_ORACLE = 11;
static constexpr uint8_t PAGE_RPS = 12;
static constexpr uint8_t PAGE_GUIDANCE = 13;
static constexpr uint8_t PAGE_LUCKY_COORD = 14;
static constexpr uint8_t PAGE_CUSTOM_IMAGE = 15;
static constexpr uint8_t PAGE_DEVICE_INFO = 16;
static constexpr uint8_t PAGE_GO_NOGO = 17;
static constexpr uint8_t PAGE_POMODORO = 18;
static constexpr uint8_t PAGE_TODO = 19;
static constexpr uint8_t PAGE_EBOOK = 20;
static constexpr uint8_t PAGE_WATER = 21;
static constexpr uint8_t PAGE_MOOD = 22;
static constexpr uint8_t PAGE_MEALS = 23;
static constexpr uint8_t PAGE_VOCAB = 24;
static constexpr uint8_t PAGE_COUNT = sizeof(pages) / sizeof(pages[0]);
static constexpr uint8_t PAGE_WIFI = PAGE_COUNT;
static constexpr uint8_t TOTAL_PAGE_COUNT = PAGE_COUNT + 1;
static constexpr uint32_t DEFAULT_PAGE_MASK = (1UL << PAGE_COUNT) - 1UL;
static const char *fieldNames[] = {"年", "月", "日", "时", "分"};
enum FoodIconIndex {
  FOOD_ICON_NOODLE,
  FOOD_ICON_RICE,
  FOOD_ICON_HOTPOT,
  FOOD_ICON_PORRIDGE,
  FOOD_ICON_DUMPLING,
  FOOD_ICON_FASTFOOD,
  FOOD_ICON_SUSHI,
  FOOD_ICON_SALAD,
  FOOD_ICON_PIZZA,
  FOOD_ICON_FISH,
  FOOD_ICON_BENTO,
  FOOD_ICON_SANDWICH
};
enum DrinkIconIndex {
  DRINK_ICON_WATER,
  DRINK_ICON_COFFEE,
  DRINK_ICON_TEA,
  DRINK_ICON_MILK_TEA,
  DRINK_ICON_JUICE,
  DRINK_ICON_SODA,
  DRINK_ICON_SOY,
  DRINK_ICON_YOGURT,
  DRINK_ICON_HOT_CHOCO
};
static const DailyItem luckItems[] = {
  {"大吉", "诸事顺遂", "适合推进"},
  {"中吉", "气势平稳", "稳中求进"},
  {"小吉", "有小收获", "先做小事"},
  {"吉", "局面向好", "可以行动"},
  {"半吉", "有利有阻", "谨慎推进"},
  {"平", "无大起落", "保持节奏"},
  {"末吉", "后势渐开", "先耐心等"},
  {"小凶", "小有阻滞", "减少冒进"},
  {"半凶", "事有反复", "留出余地"},
  {"凶", "阻力明显", "暂缓决定"},
  {"大凶", "不宜强行", "先避风险"}
};
static constexpr uint8_t LUCK_COUNT = sizeof(luckItems) / sizeof(luckItems[0]);
static const DailyItem foodItems[] = {
  {"牛肉面", "热汤稳心", "适合正餐"},
  {"番茄鸡蛋面", "酸甜温和", "简单可靠"},
  {"日式拉面", "浓汤补能", "适合慢吃"},
  {"炸酱面", "咸香顶饱", "加点黄瓜"},
  {"馄饨", "轻盈热乎", "适合早晚"},
  {"饺子", "完整一餐", "蘸料适中"},
  {"盖浇饭", "省心实用", "优先有菜"},
  {"咖喱饭", "稳定满足", "适合补能"},
  {"卤肉饭", "浓郁踏实", "配青菜好"},
  {"炒饭", "朴素可靠", "加蛋更稳"},
  {"煲仔饭", "香气很足", "别急着吃"},
  {"麻辣烫", "自由组合", "少拿丸子"},
  {"冒菜", "热辣开胃", "加点蔬菜"},
  {"火锅", "适合聚餐", "少点一点"},
  {"砂锅", "暖胃耐吃", "适合冷天"},
  {"粥", "清淡修复", "配点蛋白"},
  {"肠粉", "轻快顺口", "适合早饭"},
  {"包子豆浆", "经典组合", "快速解决"},
  {"三明治", "轻便高效", "适合赶路"},
  {"汉堡", "效率优先", "配水更好"},
  {"披萨", "适合分享", "别吃太晚"},
  {"寿司", "清爽方便", "注意新鲜"},
  {"烤肉饭", "能量充足", "配菜平衡"},
  {"沙拉鸡胸", "清爽克制", "加主食更稳"},
  {"酸菜鱼", "开胃下饭", "注意别烫"},
  {"黄焖鸡", "浓香下饭", "米饭少半碗"},
  {"烧麦", "小而顶饱", "配茶合适"},
  {"拌饭", "颜色丰富", "拌匀再吃"},
  {"烤冷面", "街边快乐", "偶尔可以"},
  {"便当", "均衡省事", "选蔬菜多的"}
};
static constexpr uint8_t FOOD_COUNT = sizeof(foodItems) / sizeof(foodItems[0]);
static const uint8_t foodIconByItem[] = {
  FOOD_ICON_NOODLE,
  FOOD_ICON_NOODLE,
  FOOD_ICON_NOODLE,
  FOOD_ICON_NOODLE,
  FOOD_ICON_DUMPLING,
  FOOD_ICON_DUMPLING,
  FOOD_ICON_RICE,
  FOOD_ICON_RICE,
  FOOD_ICON_RICE,
  FOOD_ICON_RICE,
  FOOD_ICON_BENTO,
  FOOD_ICON_HOTPOT,
  FOOD_ICON_HOTPOT,
  FOOD_ICON_HOTPOT,
  FOOD_ICON_HOTPOT,
  FOOD_ICON_PORRIDGE,
  FOOD_ICON_RICE,
  FOOD_ICON_DUMPLING,
  FOOD_ICON_SANDWICH,
  FOOD_ICON_FASTFOOD,
  FOOD_ICON_PIZZA,
  FOOD_ICON_SUSHI,
  FOOD_ICON_RICE,
  FOOD_ICON_SALAD,
  FOOD_ICON_FISH,
  FOOD_ICON_RICE,
  FOOD_ICON_DUMPLING,
  FOOD_ICON_BENTO,
  FOOD_ICON_NOODLE,
  FOOD_ICON_BENTO
};
static const DailyItem drinkItems[] = {
  {"白水", "最稳选择", "先喝一杯"},
  {"温水", "温和补水", "适合全天"},
  {"矿泉水", "清爽直接", "少点负担"},
  {"热拿铁", "柔和提神", "适合上午"},
  {"美式咖啡", "清醒推进", "午前更好"},
  {"冷萃咖啡", "干净利落", "慢慢喝完"},
  {"卡布奇诺", "轻盈奶香", "适合休息"},
  {"摩卡", "甜感安抚", "别太晚喝"},
  {"乌龙茶", "清醒不躁", "适合饭后"},
  {"绿茶", "清爽利落", "少泡太浓"},
  {"红茶", "温暖稳定", "适合下午"},
  {"普洱茶", "厚重回甘", "配点小食"},
  {"茉莉花茶", "香气舒展", "适合放松"},
  {"柠檬茶", "酸甜清醒", "适合午后"},
  {"奶茶三分糖", "甜度克制", "快乐刚好"},
  {"豆浆", "温和饱腹", "适合早上"},
  {"酸奶", "清爽顺口", "别空腹太多"},
  {"椰子水", "轻甜补水", "适合出门"},
  {"气泡水", "清爽解腻", "不必加糖"},
  {"可乐", "短暂快乐", "偶尔可以"},
  {"姜茶", "暖身驱寒", "适合冷天"},
  {"蜂蜜水", "温润舒缓", "少放一点"},
  {"橙汁", "酸甜明亮", "适合早餐"},
  {"热巧克力", "浓厚安定", "适合夜晚"}
};
static constexpr uint8_t DRINK_COUNT = sizeof(drinkItems) / sizeof(drinkItems[0]);
static constexpr uint32_t DEFAULT_FOOD_MASK = (1UL << FOOD_COUNT) - 1UL;
static constexpr uint32_t DEFAULT_DRINK_MASK = (1UL << DRINK_COUNT) - 1UL;
static const uint8_t drinkIconByItem[] = {
  DRINK_ICON_WATER,
  DRINK_ICON_WATER,
  DRINK_ICON_WATER,
  DRINK_ICON_COFFEE,
  DRINK_ICON_COFFEE,
  DRINK_ICON_COFFEE,
  DRINK_ICON_COFFEE,
  DRINK_ICON_COFFEE,
  DRINK_ICON_TEA,
  DRINK_ICON_TEA,
  DRINK_ICON_TEA,
  DRINK_ICON_TEA,
  DRINK_ICON_TEA,
  DRINK_ICON_JUICE,
  DRINK_ICON_MILK_TEA,
  DRINK_ICON_SOY,
  DRINK_ICON_YOGURT,
  DRINK_ICON_JUICE,
  DRINK_ICON_SODA,
  DRINK_ICON_SODA,
  DRINK_ICON_TEA,
  DRINK_ICON_WATER,
  DRINK_ICON_JUICE,
  DRINK_ICON_HOT_CHOCO
};
static const TarotCard tarotCards[] = {
  {"愚者", "新的开始", "保持开放", "鲁莽或迟疑", "先看清风险"},
  {"魔术师", "资源可用", "主动运用能力", "力量分散", "重整方法"},
  {"女祭司", "直觉与隐藏", "等待信息浮现", "忽略直觉", "回到内在判断"},
  {"皇后", "滋养与丰盛", "照顾关系与身体", "过度消耗", "先补足自己"},
  {"皇帝", "秩序与责任", "建立清晰边界", "控制或僵硬", "调整规则"},
  {"教皇", "传统与学习", "遵循成熟经验", "不合旧框架", "寻找真实信念"},
  {"恋人", "选择与联结", "忠于重要价值", "关系失衡", "重新校准选择"},
  {"战车", "意志与推进", "集中目标前进", "方向失控", "先稳住节奏"},
  {"力量", "勇气与耐心", "温和地坚持", "自疑或压抑", "找回内在力量"},
  {"隐者", "沉思与寻找", "给自己安静", "孤立或停滞", "适度寻求回应"},
  {"命运之轮", "变化与周期", "顺势接受转变", "阻力与反复", "不要强控结果"},
  {"正义", "因果与公平", "诚实面对事实", "偏见或逃避", "修正判断"},
  {"倒吊人", "暂停与换位", "从新角度观察", "拖延或抗拒", "分清等待和逃避"},
  {"死神", "结束与转化", "放下旧阶段", "抗拒改变", "允许更新发生"},
  {"节制", "平衡与调和", "慢慢整合资源", "失衡或过量", "回到中道"},
  {"恶魔", "束缚与执着", "看见依赖模式", "松绑与释放", "恢复自主"},
  {"高塔", "突变与破除", "接受必要重建", "逃避崩塌", "面对根本问题"},
  {"星星", "希望与疗愈", "信任长期恢复", "信心不足", "重新连接愿望"},
  {"月亮", "迷雾与潜意识", "谨慎辨认真相", "恐惧渐散", "让事实落地"},
  {"太阳", "清晰与活力", "坦然表达自己", "快乐被遮住", "降低期待负担"},
  {"审判", "觉醒与召唤", "回应内心决定", "自责或迟疑", "接受新的身份"},
  {"世界", "完成与整合", "收尾并进入新章", "未完成感", "补上最后一步"}
};
static constexpr uint8_t TAROT_COUNT = sizeof(tarotCards) / sizeof(tarotCards[0]);
static const DiceResult diceResults[] = {
  {1, 1, "大失败", "局势明显不利", "停止当前路径"},
  {2, 7, "失败", "阻力大于收益", "换方案再行动"},
  {8, 10, "代价成功", "可以推进一半", "接受必要成本"},
  {11, 15, "成功", "条件基本成立", "按计划执行"},
  {16, 19, "大成功", "优势已经形成", "主动扩大成果"},
  {20, 20, "关键成功", "机会非常明确", "立即抓住窗口"}
};
static constexpr uint8_t DICE_RESULT_COUNT = sizeof(diceResults) / sizeof(diceResults[0]);

static const DailyItem keywordItems[] = {
  {"行动", "先完成最小一步", "不要等待完美"},
  {"耐心", "给结果一点时间", "保持稳定节奏"},
  {"边界", "先确认你的底线", "再决定是否答应"},
  {"专注", "一次只推进一件事", "减少无关切换"},
  {"整理", "清掉一个旧负担", "给新计划腾位置"},
  {"连接", "主动联系一个人", "把想法说清楚"},
  {"观察", "先收集事实", "暂时不要下结论"},
  {"取舍", "保留真正重要的", "其余可以放下"},
  {"复盘", "看看哪里最费力", "调整方法再继续"},
  {"休息", "恢复也是进度", "今天留一点空白"},
  {"尝试", "允许一次小实验", "用结果代替猜测"},
  {"诚实", "承认真实的需要", "表达要具体"},
  {"节制", "够用就先停", "避免过度消耗"},
  {"勇气", "面对那件小难事", "做完会更轻松"},
  {"完成", "先把尾巴收好", "再开启下一件事"},
  {"好奇", "换个角度提问", "答案可能在细节里"}
};
static constexpr uint8_t KEYWORD_COUNT = sizeof(keywordItems) / sizeof(keywordItems[0]);

static const DailyItem oracleItems[] = {
  {"可以", "条件已经基本具备", "先走出第一步"},
  {"不建议", "当前阻力偏大", "换个时间再看"},
  {"再等等", "信息还没有齐全", "不要急着定论"},
  {"换方法", "目标未必有问题", "调整实现路径"},
  {"先问清楚", "关键在沟通", "确认对方想法"},
  {"小步尝试", "适合低成本验证", "根据反馈决定"},
  {"现在行动", "窗口已经出现", "不要继续拖延"},
  {"交给直觉", "理性信息相近", "选择更想承担的"}
};
static constexpr uint8_t ORACLE_COUNT = sizeof(oracleItems) / sizeof(oracleItems[0]);

static const DailyItem rpsItems[] = {
  {"石头", "稳住阵地", "克制剪刀"},
  {"剪刀", "果断切开", "克制布"},
  {"布", "展开空间", "克制石头"}
};
static constexpr uint8_t RPS_COUNT = sizeof(rpsItems) / sizeof(rpsItems[0]);

static const char *goodActions[] = {
  "整理桌面", "完成旧任务", "主动沟通", "短途出门", "学习新技巧",
  "制定计划", "处理账目", "运动散步", "早点休息", "尝试新口味",
  "表达感谢", "减少一件负担"
};
static const char *avoidActions[] = {
  "冲动消费", "仓促承诺", "熬夜硬撑", "同时多开", "争论输赢",
  "忽略细节", "过度解释", "拖延回复", "空腹咖啡", "临时冒险",
  "情绪决定", "反复内耗"
};
static constexpr uint8_t GOOD_ACTION_COUNT = sizeof(goodActions) / sizeof(goodActions[0]);
static constexpr uint8_t AVOID_ACTION_COUNT = sizeof(avoidActions) / sizeof(avoidActions[0]);

static const char *luckyDirections[] = {"东", "南", "西", "北", "东北", "东南", "西北", "西南"};
static const char *luckyColors[] = {"黑", "白", "红", "蓝", "绿", "金", "银", "紫"};
static constexpr uint8_t LUCKY_DIRECTION_COUNT = sizeof(luckyDirections) / sizeof(luckyDirections[0]);
static constexpr uint8_t LUCKY_COLOR_COUNT = sizeof(luckyColors) / sizeof(luckyColors[0]);
static const DailyItem moodItems[] = {
  {"开心", "状态不错", "把好心情留住"},
  {"平静", "节奏安稳", "照常向前就好"},
  {"一般", "没有大起落", "普通也很正常"},
  {"疲惫", "能量有点低", "给自己留余量"},
  {"焦虑", "思绪有点多", "先完成一件小事"},
  {"愤怒", "情绪正在升高", "暂停一下再决定"},
  {"难过", "今天不太轻松", "可以允许自己慢点"}
};
static constexpr uint8_t MOOD_COUNT = sizeof(moodItems) / sizeof(moodItems[0]);

Cz04Canvas canvas;
U8G2_FOR_ADAFRUIT_GFX u8g2;
Preferences prefs;
WebServer server(80);

uint8_t currentPage = 0;
uint32_t enabledPageMask = DEFAULT_PAGE_MASK;
bool settingTime = false;
uint8_t editField = 0;
DateTimeValue editTime = {2026, 1, 1, 0, 0, 0};
uint32_t lastClockRefreshMs = 0;
bool screenOff = false;
volatile uint8_t pendingButtonMask = 0;
volatile uint8_t armedButtonMask = BUTTON_ALL_MASK;
uint32_t buttonReleaseStartedMs[4] = {0, 0, 0, 0};
portMUX_TYPE buttonInterruptMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t currentTarotDateKey = 0;
uint8_t currentTarotRaw = 0;
bool tarotReady = false;
uint32_t currentDiceDateKey = 0;
uint8_t currentDiceRoll = 1;
bool diceReady = false;
RandomState d6State = {0, 1, false};
RandomState coinState = {0, 0, false};
RandomState randomNumberState = {0, 1, false};
RandomState oracleState = {0, 0, false};
RandomState rpsState = {0, 0, false};
RandomState luckyNumberState = {0, 1, false};
RandomState goNoGoState = {0, 0, false};
uint32_t currentLuckDateKey = 0;
uint8_t currentLuckIndex = 0;
bool luckReady = false;
uint32_t currentFoodDateKey = 0;
uint8_t currentFoodIndex = 0;
bool foodReady = false;
uint32_t currentDrinkDateKey = 0;
uint8_t currentDrinkIndex = 0;
bool drinkReady = false;
uint32_t enabledFoodMask = DEFAULT_FOOD_MASK;
uint32_t enabledDrinkMask = DEFAULT_DRINK_MASK;
String customFoodItems[CUSTOM_OPTION_MAX];
String customDrinkItems[CUSTOM_OPTION_MAX];
uint8_t customFoodCount = 0;
uint8_t customDrinkCount = 0;
uint32_t currentKeywordDateKey = 0;
uint8_t currentKeywordIndex = 0;
bool keywordReady = false;
uint32_t currentGuidanceDateKey = 0;
uint8_t currentGoodActionIndex = 0;
uint8_t currentAvoidActionIndex = 0;
bool guidanceReady = false;
uint32_t currentLuckyDateKey = 0;
uint8_t currentLuckyDirectionIndex = 0;
uint8_t currentLuckyColorIndex = 0;
bool luckyCoordReady = false;
bool fileSystemReady = false;
BatteryStatus battery = {false, false, false, false, 0, 0};
uint32_t lastBatteryReadMs = 0;
bool lowBatteryAlerted = false;
bool otaUploadAuthorized = false;
bool otaUploadFailed = false;
bool otaUploadComplete = false;
char otaUploadError[192] = {};
PomodoroMode pomodoroMode = POMODORO_FOCUS;
bool pomodoroRunning = false;
uint32_t pomodoroRemainingMs = POMODORO_FOCUS_MS;
uint32_t pomodoroStartedMs = 0;
uint32_t pomodoroLastRenderMs = 0;
uint16_t pomodoroCompleted = 0;
TodoItem todoItems[TODO_MAX_ITEMS];
uint8_t todoCount = 0;
bool todosLoaded = false;
bool bookReaderActive = false;
uint32_t bookPageIndex = 0;
uint32_t bookCurrentOffset = 0;
uint32_t bookNextOffset = 0;
bool bookHasNext = false;
uint16_t waterCupMl = 250;
uint16_t waterGoalMl = 2000;
uint16_t waterCups = 0;
uint32_t waterDate = 0;
uint8_t moodIndex = 2;
uint32_t lastMealRefreshMs = 0;
uint32_t lastMealCheckMs = 0;
uint32_t mealReminderDate = 0;
uint8_t mealReminderMask = 0;
uint8_t vocabDeck = 0;
uint8_t vocabDailyNew = VOCAB_DEFAULT_DAILY_NEW;
uint8_t vocabIndexedDeck = 0xFF;
uint16_t vocabWordCount = 0;
uint16_t vocabQueue[VOCAB_QUEUE_MAX] = {0};
uint8_t vocabQueueCount = 0;
uint8_t vocabQueuePosition = 0;
bool vocabStudyActive = false;
bool vocabMeaningVisible = false;
String vocabCurrentWord;
String vocabCurrentMeaning;
File contentUploadFile;
String contentUploadError;
String contentUploadTarget;
bool contentUploadComplete = false;
bool contentUploadStarted = false;
bool contentUploadIsVocab = false;
uint32_t contentUploadBytes = 0;
uint32_t contentUploadLimit = 0;
uint16_t contentUploadWords = 0;
String contentUploadLine;
static const char *CONTENT_UPLOAD_PATH = "/content.upload";

uint32_t contentPartitionBytes() {
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  return partition ? partition->size : 0;
}

bool largeContentStorage() {
  return contentPartitionBytes() > 1024UL * 1024UL;
}

uint32_t contentFreeBytes() {
  if (!fileSystemReady) return 0;
  uint32_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}

uint32_t contentUploadMax(bool vocabulary) {
  return storageUploadLimit(largeContentStorage(), vocabulary, contentFreeBytes());
}

uint16_t vocabWordLimit() {
  return largeContentStorage() ? 20000 : 5000;
}

const char *vocabIndexPath() {
  return vocabDeck == 1 ? "/vocab_ielts.idx" : "/vocab_cet6.idx";
}

bool isPageEnabled(uint8_t page) {
  if (page == PAGE_WIFI || page == 0) return true;
  if (page >= PAGE_COUNT) return false;
  return (enabledPageMask & (1UL << page)) != 0;
}

uint8_t decToBcd(uint8_t value) {
  return ((value / 10) << 4) | (value % 10);
}

uint8_t bcdToDec(uint8_t value) {
  return ((value >> 4) * 10) + (value & 0x0F);
}

bool i2cRead8(uint8_t addr, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, 1) != 1) return false;
  value = Wire.read();
  return true;
}

bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

uint8_t estimateBatteryPercent(uint16_t mv) {
  // Practical loaded-voltage curve for a 3.7V nominal, 4.2V Li-ion cell.
  if (mv >= 4150) return 100;
  if (mv <= 3000) return 0;
  if (mv >= 4050) return 90 + (mv - 4050) * 10 / 100;
  if (mv >= 3950) return 78 + (mv - 3950) * 12 / 100;
  if (mv >= 3850) return 65 + (mv - 3850) * 13 / 100;
  if (mv >= 3750) return 50 + (mv - 3750) * 15 / 100;
  if (mv >= 3650) return 35 + (mv - 3650) * 15 / 100;
  if (mv >= 3550) return 20 + (mv - 3550) * 15 / 100;
  if (mv >= 3450) return 8 + (mv - 3450) * 12 / 100;
  if (mv >= 3300) return 4 + (mv - 3300) * 4 / 150;
  if (mv >= 3150) return 2 + (mv - 3150) * 2 / 150;
  return (mv - 3000) * 2 / 150;
}

bool readBatteryStatus(BatteryStatus &status) {
  const bool chargeActiveLow = digitalRead(CHARGE_ACTIVE_PIN) == LOW;
  const bool chargeDoneLow = digitalRead(CHARGE_DONE_PIN) == LOW;
  status.charging = chargeActiveLow && !chargeDoneLow;
  status.chargeComplete = !chargeActiveLow && chargeDoneLow;
  status.usbPowered = chargeActiveLow || chargeDoneLow;

  analogReadMilliVolts(BATTERY_ADC_PIN);  // Discard the first sample after a long idle.
  delay(2);
  uint32_t adcMillivoltSum = 0;
  for (uint8_t i = 0; i < BATTERY_ADC_SAMPLE_COUNT; ++i) {
    adcMillivoltSum += analogReadMilliVolts(BATTERY_ADC_PIN);
    if (i + 1 < BATTERY_ADC_SAMPLE_COUNT) delay(2);
  }

  const uint32_t adcMillivolts = adcMillivoltSum / BATTERY_ADC_SAMPLE_COUNT;
  uint32_t batteryMillivolts =
      adcMillivolts * (BATTERY_DIVIDER_TOP_KOHM + BATTERY_DIVIDER_BOTTOM_KOHM) /
      BATTERY_DIVIDER_BOTTOM_KOHM;
  batteryMillivolts = batteryMillivolts * BATTERY_ADC_CALIBRATION_PERMILLE / 1000UL;
  status.present = batteryMillivolts >= 2500 && batteryMillivolts <= 4500;
  status.millivolts = status.present ? (uint16_t)batteryMillivolts : 0;
  if (!status.present) {
    status.percent = 0;
    return false;
  }

  status.percent = estimateBatteryPercent(status.millivolts);
  if (status.chargeComplete) status.percent = 100;
  Serial.printf("Battery: %u mV, %u%%, present=%u, charging=%u, full=%u, usb=%u\n",
                status.millivolts, status.percent, status.present,
                status.charging, status.chargeComplete, status.usbPowered);
  return true;
}

void updateBatteryStatus(bool force) {
  uint32_t now = millis();
  if (!force && now - lastBatteryReadMs < 30000) return;
  readBatteryStatus(battery);
  lastBatteryReadMs = now;
}

void vibrate(uint16_t ms) {
  digitalWrite(MOTOR_PIN, HIGH);
  delay(ms);
  digitalWrite(MOTOR_PIN, LOW);
}

void vibrateConfirm() {
  vibrate(150);
}

void vibrateLowBattery() {
  vibrate(100);
  delay(100);
  vibrate(100);
}

void checkLowBatteryAlert() {
  updateBatteryStatus(false);
  if (!battery.present) return;

  if (battery.charging || battery.usbPowered || battery.percent >= LOW_BATTERY_RESET_PERCENT) {
    lowBatteryAlerted = false;
    return;
  }

  if (!lowBatteryAlerted && battery.percent <= LOW_BATTERY_ALERT_PERCENT) {
    vibrateLowBattery();
    lowBatteryAlerted = true;
  }
}

uint32_t dateKey(const DateTimeValue &dt) {
  return (uint32_t)dt.year * 10000UL + (uint32_t)dt.month * 100UL + dt.day;
}

uint32_t mix32(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352dUL;
  value ^= value >> 15;
  value *= 0x846ca68bUL;
  value ^= value >> 16;
  return value;
}

bool isLeapYear(uint16_t year) {
  return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) return 29;
  if (month < 1 || month > 12) return 31;
  return days[month - 1];
}

void clampDate(DateTimeValue &dt) {
  if (dt.month < 1) dt.month = 1;
  if (dt.month > 12) dt.month = 12;
  uint8_t maxDay = daysInMonth(dt.year, dt.month);
  if (dt.day < 1) dt.day = 1;
  if (dt.day > maxDay) dt.day = maxDay;
  if (dt.hour > 23) dt.hour = 23;
  if (dt.minute > 59) dt.minute = 59;
  if (dt.second > 59) dt.second = 59;
}

bool rtcRead(DateTimeValue &dt) {
  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission() != 0) return false;

  if (Wire.requestFrom((int)PCF8563_ADDR, 7) != 7) return false;

  uint8_t seconds = Wire.read();
  if (seconds & 0x80) return false;
  dt.second = bcdToDec(seconds & 0x7F);
  dt.minute = bcdToDec(Wire.read() & 0x7F);
  dt.hour = bcdToDec(Wire.read() & 0x3F);
  dt.day = bcdToDec(Wire.read() & 0x3F);
  Wire.read();
  dt.month = bcdToDec(Wire.read() & 0x1F);
  dt.year = 2000 + bcdToDec(Wire.read());
  clampDate(dt);
  return true;
}

bool rtcWrite(const DateTimeValue &dt) {
  DateTimeValue value = dt;
  clampDate(value);

  if (!i2cWrite8(PCF8563_ADDR, 0x00, 0x00)) return false;
  if (!i2cWrite8(PCF8563_ADDR, 0x01, 0x00)) return false;

  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(0x02);
  Wire.write(decToBcd(value.second));
  Wire.write(decToBcd(value.minute));
  Wire.write(decToBcd(value.hour));
  Wire.write(decToBcd(value.day));
  Wire.write(decToBcd(1));
  Wire.write(decToBcd(value.month));
  Wire.write(decToBcd(value.year % 100));
  return Wire.endTransmission() == 0;
}

uint8_t dailyTarotRaw(uint32_t key) {
  return mix32(key ^ 0x5441524FUL) % (TAROT_COUNT * 2);
}

uint8_t dailyDiceRoll(uint32_t key) {
  return (mix32(key ^ 0x44323021UL) % 20) + 1;
}

uint8_t dailyIndex(uint32_t key, uint32_t salt, uint8_t count) {
  return mix32(key ^ salt) % count;
}

bool ensureDailyIndex(bool forceRefresh, const char *dateKeyName, const char *indexKeyName,
                      uint32_t salt, uint8_t count, uint32_t &currentDate, uint8_t &currentIndex) {
  DateTimeValue now;
  if (!rtcRead(now)) return false;

  uint32_t today = dateKey(now);
  uint32_t storedDate = prefs.getUInt(dateKeyName, 0);
  uint8_t storedIndex = prefs.getUChar(indexKeyName, 255);

  if (!forceRefresh && storedDate == today && storedIndex < count) {
    currentDate = storedDate;
    currentIndex = storedIndex;
    return true;
  }

  currentDate = today;
  currentIndex = forceRefresh ? (uint8_t)(esp_random() % count) : dailyIndex(today, salt, count);
  if (forceRefresh && storedDate == today && currentIndex == storedIndex && count > 1) {
    currentIndex = (currentIndex + 1) % count;
  }
  prefs.putUInt(dateKeyName, currentDate);
  prefs.putUChar(indexKeyName, currentIndex);
  return true;
}

bool ensureTarotSelection(bool forceRefresh) {
  DateTimeValue now;
  if (!rtcRead(now)) {
    tarotReady = false;
    return false;
  }

  uint32_t today = dateKey(now);
  uint32_t storedDate = prefs.getUInt("tarotDate", 0);
  uint8_t storedRaw = prefs.getUChar("tarotRaw", 255);

  if (!forceRefresh && storedDate == today && storedRaw < TAROT_COUNT * 2) {
    currentTarotDateKey = storedDate;
    currentTarotRaw = storedRaw;
    tarotReady = true;
    return true;
  }

  currentTarotDateKey = today;
  currentTarotRaw = forceRefresh ? (uint8_t)(esp_random() % (TAROT_COUNT * 2)) : dailyTarotRaw(today);
  if (forceRefresh && storedDate == today && currentTarotRaw == storedRaw) {
    currentTarotRaw = (currentTarotRaw + 1) % (TAROT_COUNT * 2);
  }
  prefs.putUInt("tarotDate", currentTarotDateKey);
  prefs.putUChar("tarotRaw", currentTarotRaw);
  tarotReady = true;
  return true;
}

bool ensureDiceSelection(bool forceRefresh) {
  DateTimeValue now;
  if (!rtcRead(now)) {
    diceReady = false;
    return false;
  }

  uint32_t today = dateKey(now);
  uint32_t storedDate = prefs.getUInt("diceDate", 0);
  uint8_t storedRoll = prefs.getUChar("diceRoll", 0);

  if (!forceRefresh && storedDate == today && storedRoll >= 1 && storedRoll <= 20) {
    currentDiceDateKey = storedDate;
    currentDiceRoll = storedRoll;
    diceReady = true;
    return true;
  }

  currentDiceDateKey = today;
  currentDiceRoll = forceRefresh ? (uint8_t)(esp_random() % 20 + 1) : dailyDiceRoll(today);
  if (forceRefresh && storedDate == today && currentDiceRoll == storedRoll) {
    currentDiceRoll = currentDiceRoll % 20 + 1;
  }
  prefs.putUInt("diceDate", currentDiceDateKey);
  prefs.putUChar("diceRoll", currentDiceRoll);
  diceReady = true;
  return true;
}

bool ensureRandomSelection(bool forceRefresh, const char *dateKeyName, const char *valueKeyName,
                           uint32_t salt, uint16_t minValue, uint16_t maxValue,
                           RandomState &state) {
  DateTimeValue now;
  if (!rtcRead(now) || minValue > maxValue) {
    state.ready = false;
    return false;
  }

  uint32_t today = dateKey(now);
  uint32_t storedDate = prefs.getUInt(dateKeyName, 0);
  uint16_t storedValue = prefs.getUShort(valueKeyName, UINT16_MAX);
  uint16_t valueCount = maxValue - minValue + 1;

  if (!forceRefresh && storedDate == today &&
      storedValue >= minValue && storedValue <= maxValue) {
    state = {storedDate, storedValue, true};
    return true;
  }

  uint16_t nextValue = minValue +
      (forceRefresh ? esp_random() % valueCount : mix32(today ^ salt) % valueCount);
  if (forceRefresh && storedDate == today && nextValue == storedValue && valueCount > 1) {
    nextValue = minValue + ((nextValue - minValue + 1) % valueCount);
  }

  state = {today, nextValue, true};
  prefs.putUInt(dateKeyName, state.dateKey);
  prefs.putUShort(valueKeyName, state.value);
  return true;
}

const DiceResult &diceResultFor(uint8_t roll) {
  for (uint8_t i = 0; i < DICE_RESULT_COUNT; i++) {
    if (roll >= diceResults[i].minRoll && roll <= diceResults[i].maxRoll) {
      return diceResults[i];
    }
  }
  return diceResults[0];
}

void IRAM_ATTR queueButtonInterrupt(uint8_t mask) {
  portENTER_CRITICAL_ISR(&buttonInterruptMux);
  if (armedButtonMask & mask) {
    armedButtonMask &= ~mask;
    pendingButtonMask |= mask;
  }
  portEXIT_CRITICAL_ISR(&buttonInterruptMux);
}

void IRAM_ATTR onNextButtonInterrupt() {
  queueButtonInterrupt(BUTTON_NEXT_MASK);
}

void IRAM_ATTR onPrevButtonInterrupt() {
  queueButtonInterrupt(BUTTON_PREV_MASK);
}

void IRAM_ATTR onOkButtonInterrupt() {
  queueButtonInterrupt(BUTTON_OK_MASK);
}

void IRAM_ATTR onBackButtonInterrupt() {
  queueButtonInterrupt(BUTTON_BACK_MASK);
}

uint8_t takePendingButton() {
  uint8_t pin = 0xFF;

  portENTER_CRITICAL(&buttonInterruptMux);
  if (pendingButtonMask & BUTTON_NEXT_MASK) {
    pendingButtonMask &= ~BUTTON_NEXT_MASK;
    pin = BUTTON_NEXT_PIN;
  } else if (pendingButtonMask & BUTTON_PREV_MASK) {
    pendingButtonMask &= ~BUTTON_PREV_MASK;
    pin = BUTTON_PREV_PIN;
  } else if (pendingButtonMask & BUTTON_OK_MASK) {
    pendingButtonMask &= ~BUTTON_OK_MASK;
    pin = BUTTON_OK_PIN;
  } else if (pendingButtonMask & BUTTON_BACK_MASK) {
    pendingButtonMask &= ~BUTTON_BACK_MASK;
    pin = BUTTON_BACK_PIN;
  }
  portEXIT_CRITICAL(&buttonInterruptMux);

  return pin;
}

void rearmReleasedButtons() {
  const uint8_t pins[] = {
      BUTTON_NEXT_PIN, BUTTON_PREV_PIN, BUTTON_OK_PIN, BUTTON_BACK_PIN};
  const uint8_t masks[] = {
      BUTTON_NEXT_MASK, BUTTON_PREV_MASK, BUTTON_OK_MASK, BUTTON_BACK_MASK};
  uint8_t armedSnapshot;

  portENTER_CRITICAL(&buttonInterruptMux);
  armedSnapshot = armedButtonMask;
  portEXIT_CRITICAL(&buttonInterruptMux);

  for (uint8_t i = 0; i < 4; i++) {
    if (armedSnapshot & masks[i]) {
      buttonReleaseStartedMs[i] = 0;
      continue;
    }

    if (digitalRead(pins[i]) == LOW) {
      buttonReleaseStartedMs[i] = 0;
      continue;
    }

    if (buttonReleaseStartedMs[i] == 0) {
      buttonReleaseStartedMs[i] = millis();
      continue;
    }

    if (millis() - buttonReleaseStartedMs[i] < BUTTON_RELEASE_STABLE_MS) continue;

    portENTER_CRITICAL(&buttonInterruptMux);
    armedButtonMask |= masks[i];
    portEXIT_CRITICAL(&buttonInterruptMux);
    buttonReleaseStartedMs[i] = 0;
  }
}

void drawText(int16_t x, int16_t y, const char *text) {
  u8g2.setCursor(x, y);
  u8g2.print(text);
}

uint8_t utf8SequenceLength(uint8_t lead) {
  if ((lead & 0x80) == 0) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

String truncateUtf8Units(const String &text, uint8_t maxUnits) {
  String result;
  uint8_t units = 0;
  for (size_t i = 0; i < text.length();) {
    uint8_t length = utf8SequenceLength((uint8_t)text[i]);
    uint8_t nextUnits = length == 1 ? 1 : 2;
    if (units + nextUnits > maxUnits) break;
    for (uint8_t j = 0; j < length && i < text.length(); j++, i++) {
      result += text[i];
    }
    units += nextUnits;
  }
  return result;
}

const char *vocabWordPath() {
  return vocabDeck == 1 ? VOCAB_IELTS_PATH : VOCAB_CET6_PATH;
}

const char *vocabProgressPath() {
  return vocabDeck == 1 ? VOCAB_IELTS_PROGRESS_PATH : VOCAB_CET6_PROGRESS_PATH;
}

const char *vocabDeckName() {
  return vocabDeck == 1 ? "雅思核心" : "六级高频";
}

const char *vocabDoneDayKey() {
  return vocabDeck == 1 ? "vDoneDayI" : "vDoneDayC";
}

const char *vocabDoneKey() {
  return vocabDeck == 1 ? "vDoneI" : "vDoneC";
}

const char *vocabNewDayKey() {
  return vocabDeck == 1 ? "vNewDayI" : "vNewDayC";
}

const char *vocabNewKey() {
  return vocabDeck == 1 ? "vNewI" : "vNewC";
}

uint16_t calendarDay(const DateTimeValue &dt) {
  uint32_t days = 0;
  for (uint16_t year = 2000; year < dt.year; year++) {
    days += isLeapYear(year) ? 366 : 365;
  }
  for (uint8_t month = 1; month < dt.month; month++) {
    days += daysInMonth(dt.year, month);
  }
  days += dt.day - 1;
  return (uint16_t)days;
}

bool getVocabToday(uint16_t &today) {
  DateTimeValue now;
  if (!rtcRead(now)) return false;
  today = calendarDay(now);
  return true;
}

bool loadVocabIndex(bool forceReload = false) {
  if (!fileSystemReady) return false;
  if (!forceReload && vocabIndexedDeck == vocabDeck && vocabWordCount > 0) return true;

  vocabWordCount = 0;
  vocabIndexedDeck = 0xFF;
  if (!LittleFS.exists(vocabWordPath())) return false;
  File file = LittleFS.open(vocabWordPath(), "r");
  if (!file || file.size() == 0) {
    if (file) file.close();
    return false;
  }

  const uint32_t fileSize = file.size();
  File index = LittleFS.open(vocabIndexPath(), "r");
  uint32_t indexedSize = 0, indexedCount = 0;
  if (!forceReload && index && index.read((uint8_t *)&indexedSize, 4) == 4 &&
      indexedSize == fileSize && index.read((uint8_t *)&indexedCount, 4) == 4) {
    uint32_t expectedSize = 8 + ((indexedCount + VOCAB_CHECKPOINT_WORDS - 1) / VOCAB_CHECKPOINT_WORDS) * 4;
    if (indexedCount > 0 && indexedCount <= vocabWordLimit() && index.size() == expectedSize) {
      vocabWordCount = indexedCount;
      index.close();
      file.close();
      vocabIndexedDeck = vocabDeck;
      return true;
    }
  }
  if (index) index.close();
  index = LittleFS.open(vocabIndexPath(), "w");
  uint32_t offset = 0;
  bool ok = index && index.write((const uint8_t *)&fileSize, 4) == 4 &&
      index.write((const uint8_t *)&indexedCount, 4) == 4 &&
      index.write((const uint8_t *)&offset, 4) == 4;
  if (ok) vocabWordCount = 1;
  uint8_t buffer[512];
  while (ok && file.available() && vocabWordCount < vocabWordLimit()) {
    uint32_t base = file.position();
    size_t count = file.read(buffer, sizeof(buffer));
    for (size_t i = 0; ok && i < count && vocabWordCount < vocabWordLimit(); i++) {
      offset = base + i + 1;
      if (buffer[i] == '\n' && offset < fileSize) {
        if (vocabWordCount % VOCAB_CHECKPOINT_WORDS == 0) {
          ok = index.write((const uint8_t *)&offset, 4) == 4;
        }
        if (ok) vocabWordCount++;
      }
    }
    if (count == 0) ok = false;
    delay(0);
  }
  indexedCount = vocabWordCount;
  if (ok) ok = index.seek(4) && index.write((const uint8_t *)&indexedCount, 4) == 4;
  if (index) index.close();
  file.close();
  if (!ok) {
    LittleFS.remove(vocabIndexPath());
    vocabWordCount = 0;
    return false;
  }
  vocabIndexedDeck = vocabDeck;
  return vocabWordCount > 0;
}

bool ensureVocabProgressFile() {
  if (!loadVocabIndex()) return false;
  const size_t expectedSize = (size_t)vocabWordCount * sizeof(VocabProgress);
  if (LittleFS.exists(vocabProgressPath())) {
    File existing = LittleFS.open(vocabProgressPath(), "r");
    bool valid = existing && existing.size() == expectedSize;
    if (existing) existing.close();
    if (valid) return true;
  }

  File file = LittleFS.open(vocabProgressPath(), "w");
  if (!file) return false;
  VocabProgress zeros[32] = {};
  size_t remaining = vocabWordCount;
  while (remaining > 0) {
    size_t count = remaining > 32 ? 32 : remaining;
    if (file.write((const uint8_t *)zeros, count * sizeof(VocabProgress)) !=
        count * sizeof(VocabProgress)) {
      file.close();
      return false;
    }
    remaining -= count;
  }
  file.close();
  return true;
}

bool readVocabProgress(File &file, uint16_t index, VocabProgress &progress) {
  progress = {0, 0, 0};
  if (!file || index >= vocabWordCount) return false;
  size_t offset = (size_t)index * sizeof(VocabProgress);
  if (file.position() != offset && !file.seek(offset)) return false;
  return file.read((uint8_t *)&progress, sizeof(progress)) == sizeof(progress);
}

bool writeVocabProgress(uint16_t index, const VocabProgress &progress) {
  if (!ensureVocabProgressFile() || index >= vocabWordCount) return false;
  File file = LittleFS.open(vocabProgressPath(), "r+");
  if (!file) return false;
  size_t offset = (size_t)index * sizeof(VocabProgress);
  bool ok = file.seek(offset) &&
      file.write((const uint8_t *)&progress, sizeof(progress)) == sizeof(progress);
  file.close();
  return ok;
}

bool readVocabWord(uint16_t index, String &word, String &meaning) {
  word = "";
  meaning = "";
  if (!loadVocabIndex() || index >= vocabWordCount) return false;
  File offsets = LittleFS.open(vocabIndexPath(), "r");
  uint32_t offset = 0;
  uint16_t checkpoint = index / VOCAB_CHECKPOINT_WORDS;
  bool indexed = offsets && offsets.seek(8 + (uint32_t)checkpoint * 4) &&
      offsets.read((uint8_t *)&offset, 4) == 4;
  if (offsets) offsets.close();
  if (!indexed) return false;
  File file = LittleFS.open(vocabWordPath(), "r");
  if (!file || !file.seek(offset)) {
    if (file) file.close();
    return false;
  }
  for (uint16_t i = checkpoint * VOCAB_CHECKPOINT_WORDS; i < index; i++) {
    file.readStringUntil('\n');
  }
  String line = file.readStringUntil('\n');
  file.close();
  line.replace("\r", "");
  int tab = line.indexOf('\t');
  if (tab <= 0) return false;
  word = line.substring(0, tab);
  meaning = line.substring(tab + 1);
  word.trim();
  meaning.trim();
  return word.length() > 0 && meaning.length() > 0;
}

bool getVocabStats(VocabStats &stats) {
  stats = {0, 0, 0};
  uint16_t today;
  if (!getVocabToday(today) || !ensureVocabProgressFile()) return false;
  File file = LittleFS.open(vocabProgressPath(), "r");
  if (!file) return false;
  stats.total = vocabWordCount;
  for (uint16_t index = 0; index < vocabWordCount; index++) {
    VocabProgress progress;
    if (!readVocabProgress(file, index, progress)) break;
    if (progress.stage == 0) continue;
    stats.learned++;
    if (progress.dueDay <= today) stats.due++;
  }
  file.close();
  return true;
}

uint16_t vocabTodayDone() {
  uint16_t today;
  if (!getVocabToday(today)) return 0;
  uint16_t storedDay = prefs.getUShort(vocabDoneDayKey(), 0xFFFF);
  if (storedDay != today) {
    prefs.putUShort(vocabDoneDayKey(), today);
    prefs.putUShort(vocabDoneKey(), 0);
    return 0;
  }
  return prefs.getUShort(vocabDoneKey(), 0);
}

void incrementVocabTodayDone() {
  uint16_t done = vocabTodayDone();
  if (done < 9999) prefs.putUShort(vocabDoneKey(), done + 1);
}

uint8_t vocabTodayNewDone() {
  uint16_t today;
  if (!getVocabToday(today)) return 0;
  uint16_t storedDay = prefs.getUShort(vocabNewDayKey(), 0xFFFF);
  if (storedDay != today) {
    prefs.putUShort(vocabNewDayKey(), today);
    prefs.putUChar(vocabNewKey(), 0);
    return 0;
  }
  return prefs.getUChar(vocabNewKey(), 0);
}

void incrementVocabTodayNewDone() {
  uint8_t done = vocabTodayNewDone();
  if (done < VOCAB_MAX_DAILY_NEW) prefs.putUChar(vocabNewKey(), done + 1);
}

void splitUtf8Lines(const String &text, String *lines, uint8_t maxLines,
                    uint8_t maxUnitsPerLine) {
  for (uint8_t i = 0; i < maxLines; i++) lines[i] = "";
  uint8_t line = 0;
  uint8_t units = 0;
  for (size_t i = 0; i < text.length() && line < maxLines;) {
    uint8_t length = utf8SequenceLength((uint8_t)text[i]);
    uint8_t tokenUnits = length == 1 ? 1 : 2;
    if (units + tokenUnits > maxUnitsPerLine) {
      line++;
      units = 0;
      continue;
    }
    for (uint8_t j = 0; j < length && i < text.length(); j++, i++) {
      lines[line] += text[i];
    }
    units += tokenUnits;
  }
}

void clearCustomOptions(String *items, uint8_t &count) {
  for (uint8_t i = 0; i < CUSTOM_OPTION_MAX; i++) items[i] = "";
  count = 0;
}

bool loadCustomOptions(const char *path, String *items, uint8_t &count) {
  clearCustomOptions(items, count);
  if (!fileSystemReady || !LittleFS.exists(path)) return true;
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  while (file.available() && count < CUSTOM_OPTION_MAX) {
    String line = file.readStringUntil('\n');
    line.replace("\r", "");
    line.replace("\t", " ");
    line.trim();
    line = truncateUtf8Units(line, CUSTOM_OPTION_UNITS);
    if (line.length() > 0) items[count++] = line;
  }
  file.close();
  return true;
}

bool saveCustomOptions(const char *path, String *items, uint8_t count) {
  if (!fileSystemReady) return false;
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  for (uint8_t i = 0; i < count; i++) file.println(items[i]);
  file.close();
  return true;
}

uint8_t activeOptionCount(uint32_t mask, uint8_t builtinCount, uint8_t customCount) {
  uint8_t count = customCount;
  for (uint8_t i = 0; i < builtinCount; i++) {
    if (mask & (1UL << i)) count++;
  }
  return count;
}

bool resolveOptionIndex(uint8_t selectedIndex, uint32_t mask, uint8_t builtinCount,
                        uint8_t customCount, bool &isCustom, uint8_t &itemIndex) {
  uint8_t cursor = 0;
  for (uint8_t i = 0; i < builtinCount; i++) {
    if ((mask & (1UL << i)) == 0) continue;
    if (cursor++ == selectedIndex) {
      isCustom = false;
      itemIndex = i;
      return true;
    }
  }
  uint8_t customIndex = selectedIndex - cursor;
  if (customIndex >= customCount) return false;
  isCustom = true;
  itemIndex = customIndex;
  return true;
}

void clearTodos() {
  for (uint8_t i = 0; i < TODO_MAX_ITEMS; i++) {
    todoItems[i].done = false;
    todoItems[i].text = "";
  }
  todoCount = 0;
}

bool loadTodos() {
  clearTodos();
  todosLoaded = true;
  if (!fileSystemReady || !LittleFS.exists(TODO_PATH)) return true;

  File file = LittleFS.open(TODO_PATH, "r");
  if (!file) return false;
  while (file.available() && todoCount < TODO_MAX_ITEMS) {
    String line = file.readStringUntil('\n');
    line.replace("\r", "");
    if (line.length() == 0) continue;

    bool done = false;
    if (line.length() >= 2 && line[1] == '\t') {
      done = line[0] == '1';
      line.remove(0, 2);
    }
    line.trim();
    if (line.length() == 0) continue;
    todoItems[todoCount++] = {done, line.substring(0, 120)};
  }
  file.close();
  return true;
}

bool saveTodos() {
  if (!fileSystemReady) return false;
  File file = LittleFS.open(TODO_PATH, "w");
  if (!file) return false;
  for (uint8_t i = 0; i < todoCount; i++) {
    file.print(todoItems[i].done ? "1\t" : "0\t");
    file.println(todoItems[i].text);
  }
  file.close();
  return true;
}

bool completeNextTodo() {
  if (!todosLoaded) loadTodos();
  for (uint8_t i = 0; i < todoCount; i++) {
    if (!todoItems[i].done) {
      todoItems[i].done = true;
      return saveTodos();
    }
  }
  return false;
}

uint32_t pomodoroCurrentRemaining() {
  if (!pomodoroRunning) return pomodoroRemainingMs;
  uint32_t elapsed = millis() - pomodoroStartedMs;
  return elapsed >= pomodoroRemainingMs ? 0 : pomodoroRemainingMs - elapsed;
}

void togglePomodoro() {
  if (pomodoroRunning) {
    pomodoroRemainingMs = pomodoroCurrentRemaining();
    pomodoroRunning = false;
    return;
  }
  if (pomodoroRemainingMs == 0) {
    pomodoroRemainingMs = pomodoroMode == POMODORO_FOCUS
        ? POMODORO_FOCUS_MS : POMODORO_BREAK_MS;
  }
  pomodoroStartedMs = millis();
  pomodoroRunning = true;
}

void resetPomodoro() {
  pomodoroRunning = false;
  pomodoroRemainingMs = pomodoroMode == POMODORO_FOCUS
      ? POMODORO_FOCUS_MS : POMODORO_BREAK_MS;
}

bool readBookPage(uint32_t offset, String lines[BOOK_LINES_PER_PAGE],
                  uint32_t &nextOffset, bool &hasNext) {
  for (uint8_t i = 0; i < BOOK_LINES_PER_PAGE; i++) lines[i] = "";
  nextOffset = offset;
  hasNext = false;
  if (!fileSystemReady || !LittleFS.exists(BOOK_PATH)) return false;

  File file = LittleFS.open(BOOK_PATH, "r");
  if (!file || !file.seek(offset)) {
    if (file) file.close();
    return false;
  }

  uint8_t lineIndex = 0;
  uint8_t lineUnits = 0;
  while (file.available() && lineIndex < BOOK_LINES_PER_PAGE) {
    uint32_t tokenOffset = file.position();
    int raw = file.read();
    if (raw < 0) break;
    uint8_t lead = (uint8_t)raw;

    if (lead == '\r' || lead == '\n') {
      if (lead == '\r' && file.peek() == '\n') file.read();
      lineIndex++;
      lineUnits = 0;
      if (lineIndex >= BOOK_LINES_PER_PAGE) break;
      continue;
    }

    uint8_t length = utf8SequenceLength(lead);
    uint8_t tokenUnits = length == 1 ? 1 : 2;
    if (lineUnits + tokenUnits > BOOK_LINE_UNITS) {
      lineIndex++;
      lineUnits = 0;
      if (lineIndex >= BOOK_LINES_PER_PAGE) {
        nextOffset = tokenOffset;
        hasNext = true;
        file.close();
        return true;
      }
    }

    lines[lineIndex] += (char)lead;
    for (uint8_t i = 1; i < length && file.available(); i++) {
      lines[lineIndex] += (char)file.read();
    }
    lineUnits += tokenUnits;
  }

  nextOffset = file.position();
  hasNext = file.available();
  file.close();
  return true;
}

void resetBookPagination() {
  bookReaderActive = false;
  bookPageIndex = 0;
  bookCurrentOffset = 0;
  bookNextOffset = 0;
  bookHasNext = false;
  LittleFS.remove(BOOK_INDEX_PATH);
}

bool saveBookCheckpoint(uint32_t page, uint32_t offset) {
  File index = LittleFS.open(BOOK_INDEX_PATH, LittleFS.exists(BOOK_INDEX_PATH) ? "r+" : "w");
  bool ok = index && index.seek((page / BOOK_CHECKPOINT_PAGES) * 4) &&
      index.write((const uint8_t *)&offset, 4) == 4;
  if (index) index.close();
  return ok;
}

bool findBookPageOffset(uint32_t page, uint32_t &offset) {
  uint32_t checkpoint = page / BOOK_CHECKPOINT_PAGES;
  offset = 0;
  if (checkpoint > 0) {
    File index = LittleFS.open(BOOK_INDEX_PATH, "r");
    bool ok = index && index.seek(checkpoint * 4) && index.read((uint8_t *)&offset, 4) == 4;
    if (index) index.close();
    if (!ok) return false;
  }
  // Sparse checkpoints bound backward navigation to at most 63 small pages.
  String lines[BOOK_LINES_PER_PAGE];
  for (uint32_t i = checkpoint * BOOK_CHECKPOINT_PAGES; i < page; i++) {
    uint32_t next;
    bool hasNext;
    if (!readBookPage(offset, lines, next, hasNext) || !hasNext) return false;
    offset = next;
    delay(0);
  }
  return true;
}

void drawPageFrame() {
  DrawLine(6, 22, 205, 22, COLOR_BLACK, TRUE, 1);
  DrawLine(6, 84, 205, 84, COLOR_LIGHT, TRUE, 1);
  updateBatteryStatus(false);
  DrawLine(176, 8, 201, 8, COLOR_BLACK, TRUE, 1);
  DrawLine(176, 17, 201, 17, COLOR_BLACK, TRUE, 1);
  DrawLine(176, 8, 176, 17, COLOR_BLACK, TRUE, 1);
  DrawLine(201, 8, 201, 17, COLOR_BLACK, TRUE, 1);
  DrawLine(202, 11, 205, 11, COLOR_BLACK, TRUE, 1);
  DrawLine(202, 14, 205, 14, COLOR_BLACK, TRUE, 1);
  DrawLine(205, 11, 205, 14, COLOR_BLACK, TRUE, 1);

  if (battery.present) {
    uint8_t fillWidth = map(battery.percent, 0, 100, 0, 21);
    for (uint8_t x = 0; x < fillWidth; x++) {
      DrawLine(179 + x, 11, 179 + x, 15, COLOR_BLACK, TRUE, 1);
    }
    if (battery.usbPowered) {
      uint8_t boltColor = fillWidth >= 10 ? COLOR_WHITE : COLOR_BLACK;
      DrawLine(187, 10, 184, 14, boltColor, TRUE, 1);
      DrawLine(184, 14, 188, 14, boltColor, TRUE, 1);
      DrawLine(188, 14, 185, 17, boltColor, TRUE, 1);
    }
  }

  char batteryText[6];
  if (battery.present) {
    snprintf(batteryText, sizeof(batteryText), "%u%%", battery.percent);
  } else {
    snprintf(batteryText, sizeof(batteryText), "--");
  }
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_4x6_tf);
  drawText(150, 16, batteryText);
}

void drawImageSlot() {
  DrawLine(148, 30, 208, 30, COLOR_BLACK, TRUE, 1);
  DrawLine(148, 90, 208, 90, COLOR_BLACK, TRUE, 1);
  DrawLine(148, 30, 148, 90, COLOR_BLACK, TRUE, 1);
  DrawLine(208, 30, 208, 90, COLOR_BLACK, TRUE, 1);
}

void drawIcon2bpp(int16_t x, int16_t y, const uint8_t *icon) {
  for (uint8_t yy = 0; yy < ICON_H; yy++) {
    for (uint8_t xx = 0; xx < ICON_W; xx++) {
      uint16_t bitIndex = yy * ICON_W + xx;
      uint8_t packed = pgm_read_byte(icon + bitIndex / 4);
      uint8_t shift = (3 - (xx & 0x03)) * 2;
      uint8_t grey = (packed >> shift) & 0x03;
      DrawPixel(x + xx, y + yy, grey, TRUE, 1);
    }
  }
}

void drawIconInSlot(const uint8_t *icon) {
  drawImageSlot();
  drawIcon2bpp(150, 32, icon);
}

const uint8_t *tarotIcon(uint8_t cardIndex) {
  return (const uint8_t *)pgm_read_ptr(&tarotIconTable[cardIndex]);
}

const uint8_t *foodIcon(uint8_t itemIndex) {
  uint8_t iconIndex = foodIconByItem[itemIndex];
  return (const uint8_t *)pgm_read_ptr(&foodIconTable[iconIndex]);
}

const uint8_t *drinkIcon(uint8_t itemIndex) {
  uint8_t iconIndex = drinkIconByItem[itemIndex];
  return (const uint8_t *)pgm_read_ptr(&drinkIconTable[iconIndex]);
}

const uint8_t *luckIcon(uint8_t itemIndex) {
  static const uint8_t luckIconMap[] = {
    0, // 大吉
    1, // 中吉
    2, // 小吉
    3, // 吉
    3, // 半吉，借用吉
    5, // 平
    4, // 末吉
    6, // 小凶
    7, // 半凶，借用凶
    7, // 凶
    8  // 大凶
  };
  uint8_t iconIndex = 5;
  if (itemIndex < sizeof(luckIconMap)) iconIndex = luckIconMap[itemIndex];
  return (const uint8_t *)pgm_read_ptr(&luckIconTable[iconIndex]);
}

void flushScreen() {
  EPaper_WriteGreyImage(Display_buffer, 0, 0, WIDTH, HEIGHT, FALSE, FALSE);
}

void enterScreenOff() {
  Fill_Screen(COLOR_WHITE, TRUE);
  flushScreen();
  screenOff = true;
}

void renderMessage(const char *title, const char *line1, const char *line2, const char *line3) {
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();

  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  drawText(8, 16, title);
  drawText(12, 42, line1);
  drawText(12, 58, line2);
  drawText(12, 74, line3);

  flushScreen();
}

void renderClockPage() {
  DateTimeValue now;
  if (!rtcRead(now)) {
    renderMessage("时钟", "PCF8563 未响应", "检查 BAT+/GND", "SDA21 SCL22");
    lastClockRefreshMs = millis();
    return;
  }

  char dateLine[24];
  char timeLine[24];
  snprintf(dateLine, sizeof(dateLine), "%04u-%02u-%02u", now.year, now.month, now.day);
  snprintf(timeLine, sizeof(timeLine), "%02u:%02u:%02u", now.hour, now.minute, now.second);

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();

  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "时钟");
  drawText(12, 40, dateLine);

  u8g2.setFont(u8g2_font_logisoso24_tn);
  drawText(12, 74, timeLine);

  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(42, 101, "10秒刷新 OK设置");

  flushScreen();
  lastClockRefreshMs = millis();
}

void renderTarotPage(bool forceRefresh) {
  if (!ensureTarotSelection(forceRefresh)) {
    renderMessage("塔罗牌", "PCF8563 未响应", "无法生成每日牌", "检查 SDA/SCL");
    return;
  }

  uint8_t cardIndex = currentTarotRaw % TAROT_COUNT;
  bool reversed = currentTarotRaw >= TAROT_COUNT;
  const TarotCard &card = tarotCards[cardIndex];

  char titleLine[32];
  snprintf(titleLine, sizeof(titleLine), "%s %s", card.name, reversed ? "逆位" : "正位");

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  drawIconInSlot(tarotIcon(cardIndex));

  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  drawText(8, 16, "塔罗牌");
  drawText(12, 38, titleLine);
  drawText(12, 55, reversed ? card.reversedMeaning : card.uprightMeaning);
  drawText(12, 72, reversed ? card.reversedAdvice : card.uprightAdvice);
  drawText(8, 101, "OK 重抽");

  flushScreen();
}

void renderDailyItemPage(const char *title, const DailyItem &item, const uint8_t *icon) {
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  drawIconInSlot(icon);

  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  drawText(8, 16, title);
  drawText(12, 38, item.name);
  drawText(12, 56, item.line1);
  drawText(12, 74, item.line2);
  drawText(8, 101, "OK 重抽");

  flushScreen();
}

void renderLuckPage(bool forceRefresh) {
  luckReady = ensureDailyIndex(forceRefresh, "luckDate", "luckIdx", 0x4C55434BUL,
                               LUCK_COUNT, currentLuckDateKey, currentLuckIndex);
  if (!luckReady) {
    renderMessage("今日运势", "PCF8563 未响应", "无法生成运势", "检查 SDA/SCL");
    return;
  }
  renderDailyItemPage("今日运势", luckItems[currentLuckIndex], luckIcon(currentLuckIndex));
}

void renderFoodPage(bool forceRefresh) {
  uint8_t optionCount = activeOptionCount(enabledFoodMask, FOOD_COUNT, customFoodCount);
  if (optionCount == 0) {
    renderMessage("今天吃什么", "候选列表为空", "请在手机网页添加", "或重新启用内置选项");
    return;
  }
  foodReady = ensureDailyIndex(forceRefresh, "foodDate", "foodIdx", 0x464F4F44UL,
                               optionCount, currentFoodDateKey, currentFoodIndex);
  if (!foodReady) {
    renderMessage("今天吃什么", "PCF8563 未响应", "无法生成推荐", "检查 SDA/SCL");
    return;
  }
  bool isCustom;
  uint8_t itemIndex;
  if (!resolveOptionIndex(currentFoodIndex, enabledFoodMask, FOOD_COUNT, customFoodCount,
                          isCustom, itemIndex)) {
    renderMessage("今天吃什么", "候选读取失败", "请在网页重新保存", "配置不会丢失");
    return;
  }
  if (isCustom) {
    DailyItem item = {customFoodItems[itemIndex].c_str(), "自定义候选", "今天就选这个吧"};
    renderDailyItemPage("今天吃什么", item, foodIcon(FOOD_COUNT - 1));
  } else {
    renderDailyItemPage("今天吃什么", foodItems[itemIndex], foodIcon(itemIndex));
  }
}

void renderDrinkPage(bool forceRefresh) {
  uint8_t optionCount = activeOptionCount(enabledDrinkMask, DRINK_COUNT, customDrinkCount);
  if (optionCount == 0) {
    renderMessage("今天喝什么", "候选列表为空", "请在手机网页添加", "或重新启用内置选项");
    return;
  }
  drinkReady = ensureDailyIndex(forceRefresh, "drinkDate", "drinkIdx", 0x4452494EUL,
                                optionCount, currentDrinkDateKey, currentDrinkIndex);
  if (!drinkReady) {
    renderMessage("今天喝什么", "PCF8563 未响应", "无法生成推荐", "检查 SDA/SCL");
    return;
  }
  bool isCustom;
  uint8_t itemIndex;
  if (!resolveOptionIndex(currentDrinkIndex, enabledDrinkMask, DRINK_COUNT, customDrinkCount,
                          isCustom, itemIndex)) {
    renderMessage("今天喝什么", "候选读取失败", "请在网页重新保存", "配置不会丢失");
    return;
  }
  if (isCustom) {
    DailyItem item = {customDrinkItems[itemIndex].c_str(), "自定义候选", "今天就喝这个吧"};
    renderDailyItemPage("今天喝什么", item, drinkIcon(0));
  } else {
    renderDailyItemPage("今天喝什么", drinkItems[itemIndex], drinkIcon(itemIndex));
  }
}

void drawFilledDot(uint8_t x, uint8_t y) {
  for (uint8_t radius = 0; radius <= 3; radius++) {
    DrawCircle(x, y, radius, COLOR_BLACK, TRUE, 1);
  }
}

void drawD6Face(uint8_t roll) {
  drawImageSlot();
  DrawLine(157, 37, 199, 37, COLOR_BLACK, TRUE, 1);
  DrawLine(157, 84, 199, 84, COLOR_BLACK, TRUE, 1);
  DrawLine(157, 37, 157, 84, COLOR_BLACK, TRUE, 1);
  DrawLine(199, 37, 199, 84, COLOR_BLACK, TRUE, 1);

  if (roll == 1 || roll == 3 || roll == 5) drawFilledDot(178, 60);
  if (roll >= 2) {
    drawFilledDot(166, 48);
    drawFilledDot(190, 72);
  }
  if (roll >= 4) {
    drawFilledDot(190, 48);
    drawFilledDot(166, 72);
  }
  if (roll == 6) {
    drawFilledDot(166, 60);
    drawFilledDot(190, 60);
  }
}

void renderD6Page(bool forceRefresh) {
  if (!ensureRandomSelection(forceRefresh, "d6Date", "d6Val", 0x44362121UL,
                             1, 6, d6State)) {
    renderMessage("六面骰子", "PCF8563 未响应", "无法保存每日骰", "检查 SDA/SCL");
    return;
  }

  char rollLine[24];
  snprintf(rollLine, sizeof(rollLine), "D6: %u", d6State.value);

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  drawD6Face((uint8_t)d6State.value);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "六面骰子");
  drawText(12, 43, rollLine);
  drawText(12, 63, d6State.value >= 4 ? "点数偏高" : "点数偏低");
  drawText(8, 101, "OK 重掷");
  flushScreen();
}

void renderD20Page(bool forceRefresh) {
  if (!ensureDiceSelection(forceRefresh)) {
    renderMessage("二十面骰子", "PCF8563 未响应", "无法生成每日骰", "检查 SDA/SCL");
    return;
  }

  const DiceResult &result = diceResultFor(currentDiceRoll);
  char rollLine[24];
  snprintf(rollLine, sizeof(rollLine), "D20: %u", currentDiceRoll);

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  drawIconInSlot(icon_dice_d20);

  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  drawText(8, 16, "二十面骰子");
  drawText(12, 38, rollLine);
  drawText(82, 38, result.grade);
  drawText(12, 58, result.meaning);
  drawText(12, 75, result.advice);
  drawText(8, 101, "OK 重掷");

  flushScreen();
}

void renderCoinPage(bool forceRefresh) {
  if (!ensureRandomSelection(forceRefresh, "coinDate", "coinVal", 0x434F494EUL,
                             0, 1, coinState)) {
    renderMessage("抛硬币", "PCF8563 未响应", "无法保存每日结果", "检查 SDA/SCL");
    return;
  }

  const char *side = coinState.value == 0 ? "正面" : "反面";
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  drawImageSlot();
  DrawCircle(178, 60, 23, COLOR_BLACK, TRUE, 1);
  DrawCircle(178, 60, 20, COLOR_BLACK, TRUE, 1);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "抛硬币");
  drawText(12, 47, side);
  drawText(12, 68, coinState.value == 0 ? "选择第一项" : "选择第二项");
  drawText(8, 101, "OK 再抛");
  drawText(166, 64, coinState.value == 0 ? "正" : "反");
  flushScreen();
}

void renderRandomNumberPage(bool forceRefresh) {
  if (!ensureRandomSelection(forceRefresh, "numDate", "numVal", 0x52313030UL,
                             1, 100, randomNumberState)) {
    renderMessage("随机数", "PCF8563 未响应", "无法保存每日结果", "检查 SDA/SCL");
    return;
  }

  char numberLine[12];
  snprintf(numberLine, sizeof(numberLine), "%u", randomNumberState.value);
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  drawImageSlot();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "随机数 1-100");
  drawText(12, 47, "本次抽取");
  drawText(12, 68, numberLine);
  drawText(8, 101, "OK 再抽");
  u8g2.setFont(u8g2_font_logisoso24_tn);
  int16_t numberWidth = u8g2.getUTF8Width(numberLine);
  drawText(178 - numberWidth / 2, 69, numberLine);
  flushScreen();
}

void renderKeywordPage(bool forceRefresh) {
  keywordReady = ensureDailyIndex(forceRefresh, "wordDate", "wordIdx", 0x574F5244UL,
                                  KEYWORD_COUNT, currentKeywordDateKey, currentKeywordIndex);
  if (!keywordReady) {
    renderMessage("今日关键词", "PCF8563 未响应", "无法生成关键词", "检查 SDA/SCL");
    return;
  }

  const DailyItem &item = keywordItems[currentKeywordIndex];
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "今日关键词");
  drawText(12, 42, item.name);
  drawText(12, 61, item.line1);
  drawText(12, 78, item.line2);
  drawText(8, 101, "OK 重抽");
  flushScreen();
}

void renderWordToolPage(const char *title, const DailyItem &item, const char *footer) {
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  drawImageSlot();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, title);
  drawText(12, 42, item.name);
  drawText(12, 61, item.line1);
  drawText(12, 78, item.line2);
  drawText(8, 101, footer);
  int16_t labelWidth = u8g2.getUTF8Width(item.name);
  drawText(178 - labelWidth / 2, 64, item.name);
  flushScreen();
}

void renderOraclePage(bool forceRefresh) {
  if (!ensureRandomSelection(forceRefresh, "oracleDate", "oracleVal", 0x4F524143UL,
                             0, ORACLE_COUNT - 1, oracleState)) {
    renderMessage("是非神谕", "PCF8563 未响应", "无法保存每日结果", "检查 SDA/SCL");
    return;
  }
  renderWordToolPage("是非神谕", oracleItems[oracleState.value], "OK 再问");
}

void drawGoNoGoIcon(bool allowed) {
  drawImageSlot();
  DrawCircle(178, 60, 25, COLOR_BLACK, TRUE, 1);
  DrawCircle(178, 60, 24, COLOR_BLACK, TRUE, 1);

  if (allowed) {
    DrawLine(159, 60, 171, 72, COLOR_BLACK, TRUE, 1);
    DrawLine(159, 61, 171, 73, COLOR_BLACK, TRUE, 1);
    DrawLine(160, 59, 172, 71, COLOR_BLACK, TRUE, 1);
    DrawLine(171, 72, 198, 45, COLOR_BLACK, TRUE, 1);
    DrawLine(171, 73, 198, 46, COLOR_BLACK, TRUE, 1);
    DrawLine(172, 71, 198, 45, COLOR_BLACK, TRUE, 1);
  } else {
    DrawLine(161, 43, 195, 77, COLOR_BLACK, TRUE, 1);
    DrawLine(161, 44, 194, 77, COLOR_BLACK, TRUE, 1);
    DrawLine(162, 43, 195, 76, COLOR_BLACK, TRUE, 1);
    DrawLine(195, 43, 161, 77, COLOR_BLACK, TRUE, 1);
    DrawLine(194, 43, 161, 76, COLOR_BLACK, TRUE, 1);
    DrawLine(195, 44, 162, 77, COLOR_BLACK, TRUE, 1);
  }
}

void renderGoNoGoPage(bool forceRefresh) {
  if (!ensureRandomSelection(forceRefresh, "goDate", "goVal", 0x474F4E4FUL,
                             0, 1, goNoGoState)) {
    renderMessage("YES / NO", "PCF8563 未响应", "无法保存每日结果", "检查 SDA/SCL");
    return;
  }

  bool allowed = goNoGoState.value == 1;
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  drawGoNoGoIcon(allowed);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "YES / NO");
  drawText(12, 43, allowed ? "YES" : "NO");
  drawText(12, 64, allowed ? "可以推进" : "建议暂停");
  drawText(8, 101, "OK 再问");
  flushScreen();
}

void renderRpsPage(bool forceRefresh) {
  if (!ensureRandomSelection(forceRefresh, "rpsDate", "rpsVal", 0x52505321UL,
                             0, RPS_COUNT - 1, rpsState)) {
    renderMessage("猜拳", "PCF8563 未响应", "无法保存每日结果", "检查 SDA/SCL");
    return;
  }
  renderWordToolPage("猜拳", rpsItems[rpsState.value], "OK 再出");
}

void renderGuidancePage(bool forceRefresh) {
  uint32_t avoidDate = currentGuidanceDateKey;
  guidanceReady = ensureDailyIndex(forceRefresh, "guideDate", "goodIdx", 0x474F4F44UL,
                                   GOOD_ACTION_COUNT, currentGuidanceDateKey, currentGoodActionIndex);
  guidanceReady = ensureDailyIndex(forceRefresh, "avoidDate", "avoidIdx", 0x41564F49UL,
                                   AVOID_ACTION_COUNT, avoidDate, currentAvoidActionIndex) && guidanceReady;
  if (!guidanceReady) {
    renderMessage("今日宜忌", "PCF8563 未响应", "无法生成今日宜忌", "检查 SDA/SCL");
    return;
  }

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "今日宜忌");
  drawText(12, 43, "宜");
  drawText(36, 43, goodActions[currentGoodActionIndex]);
  drawText(12, 68, "忌");
  drawText(36, 68, avoidActions[currentAvoidActionIndex]);
  drawText(8, 101, "OK 重抽");
  flushScreen();
}

void renderLuckyCoordPage(bool forceRefresh) {
  uint32_t colorDate = currentLuckyDateKey;
  luckyCoordReady = ensureDailyIndex(forceRefresh, "luckyDate", "dirIdx", 0x44495221UL,
                                     LUCKY_DIRECTION_COUNT, currentLuckyDateKey,
                                     currentLuckyDirectionIndex);
  luckyCoordReady = ensureDailyIndex(forceRefresh, "colorDate", "colorIdx", 0x434F4C52UL,
                                     LUCKY_COLOR_COUNT, colorDate, currentLuckyColorIndex) && luckyCoordReady;
  luckyCoordReady = ensureRandomSelection(forceRefresh, "luckNDate", "luckNVal", 0x4E554D39UL,
                                          1, 9, luckyNumberState) && luckyCoordReady;
  if (!luckyCoordReady) {
    renderMessage("幸运坐标", "PCF8563 未响应", "无法生成幸运坐标", "检查 SDA/SCL");
    return;
  }

  char numberLine[20];
  char directionLine[24];
  char colorLine[24];
  snprintf(numberLine, sizeof(numberLine), "数字: %u", luckyNumberState.value);
  snprintf(directionLine, sizeof(directionLine), "方位: %s", luckyDirections[currentLuckyDirectionIndex]);
  snprintf(colorLine, sizeof(colorLine), "色彩: %s", luckyColors[currentLuckyColorIndex]);
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "幸运坐标");
  drawText(12, 40, numberLine);
  drawText(12, 59, directionLine);
  drawText(12, 78, colorLine);
  drawText(8, 101, "OK 重抽");
  flushScreen();
}

void renderCustomImagePage() {
  if (!fileSystemReady || !LittleFS.exists(CUSTOM_IMAGE_PATH)) {
    renderMessage("我的图片", "还没有保存图片", "连接设备热点", "网页上传后会保留");
    return;
  }

  File imageFile = LittleFS.open(CUSTOM_IMAGE_PATH, "r");
  if (!imageFile || imageFile.size() != CUSTOM_IMAGE_BYTES) {
    if (imageFile) imageFile.close();
    renderMessage("我的图片", "图片文件无效", "请从网页重新上传", "需要 212x104");
    return;
  }

  Fill_Screen(COLOR_WHITE, TRUE);
  uint32_t pixelIndex = 0;
  while (imageFile.available() && pixelIndex < 212UL * 104UL) {
    uint8_t packed = imageFile.read();
    for (int8_t bit = 7; bit >= 0 && pixelIndex < 212UL * 104UL; bit--, pixelIndex++) {
      if (packed & (1U << bit)) {
        DrawPixel(pixelIndex % 212, pixelIndex / 212, COLOR_BLACK, TRUE, 1);
      }
    }
  }
  imageFile.close();
  flushScreen();
}

void renderDeviceInfoPage() {
  char versionLine[28];
  char memoryLine[32];
  char uptimeLine[32];
  char wifiLine[28];
  uint32_t uptimeSeconds = millis() / 1000;
  snprintf(versionLine, sizeof(versionLine), "固件: %s", FIRMWARE_VERSION);
  snprintf(memoryLine, sizeof(memoryLine), "可用内存: %luKB",
           (unsigned long)(ESP.getFreeHeap() / 1024));
  snprintf(uptimeLine, sizeof(uptimeLine), "运行: %lu时%lu分",
           (unsigned long)(uptimeSeconds / 3600),
           (unsigned long)((uptimeSeconds / 60) % 60));
  snprintf(wifiLine, sizeof(wifiLine), "WiFi设备: %u", WiFi.softAPgetStationNum());

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "设备状态");
  drawText(12, 36, versionLine);
  drawText(12, 51, memoryLine);
  drawText(12, 66, uptimeLine);
  drawText(12, 81, wifiLine);
  drawText(8, 101, "OK 刷新");
  flushScreen();
}

void renderPomodoroPage() {
  uint32_t remaining = pomodoroCurrentRemaining();
  uint32_t totalSeconds = (remaining + 999) / 1000;
  uint16_t minutes = totalSeconds / 60;
  uint8_t seconds = totalSeconds % 60;
  char timeLine[12];
  char sessionLine[28];
  snprintf(timeLine, sizeof(timeLine), "%02u:%02u", minutes, seconds);
  snprintf(sessionLine, sizeof(sessionLine), "已完成 %u 轮", pomodoroCompleted);

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, pomodoroMode == POMODORO_FOCUS ? "番茄钟 · 专注" : "番茄钟 · 休息");
  drawText(132, 42, pomodoroRunning ? "进行中" : "已暂停");
  drawText(132, 61, sessionLine);
  drawText(8, 101, "OK 开始/暂停  BACK重置");
  u8g2.setFont(u8g2_font_logisoso24_tn);
  drawText(12, 68, timeLine);
  flushScreen();
  pomodoroLastRenderMs = millis();
}

void renderTodoPage() {
  if (!todosLoaded) loadTodos();
  uint8_t doneCount = 0;
  for (uint8_t i = 0; i < todoCount; i++) {
    if (todoItems[i].done) doneCount++;
  }

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  char titleLine[28];
  snprintf(titleLine, sizeof(titleLine), "待办事项 %u/%u", doneCount, todoCount);
  drawText(8, 16, titleLine);
  if (todoCount == 0) {
    drawText(12, 47, "暂无待办");
    drawText(12, 68, "请在手机网页添加");
  } else {
    const int16_t rows[] = {39, 57, 75};
    for (uint8_t i = 0; i < todoCount && i < 3; i++) {
      String line = todoItems[i].done ? "[x] " : "[ ] ";
      line += truncateUtf8Units(todoItems[i].text, 25);
      drawText(10, rows[i], line.c_str());
    }
  }
  drawText(8, 101, "OK 完成下一项 · 手机可同步");
  flushScreen();
}

void renderBookReaderPage() {
  String lines[BOOK_LINES_PER_PAGE];
  if (!readBookPage(bookCurrentOffset, lines, bookNextOffset, bookHasNext)) {
    bookReaderActive = false;
    renderMessage("电子书", "无法读取文本", "请从手机重新上传", "仅支持 UTF-8 TXT");
    return;
  }

  String title = prefs.getString("bookTitle", "电子书");
  title = truncateUtf8Units(title, 24);
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, title.c_str());
  const int16_t rows[] = {34, 46, 58, 70, 82};
  for (uint8_t i = 0; i < BOOK_LINES_PER_PAGE; i++) {
    drawText(8, rows[i], lines[i].c_str());
  }
  char footer[32];
  snprintf(footer, sizeof(footer), "第%lu页  OK退出", (unsigned long)bookPageIndex + 1);
  drawText(8, 101, footer);
  flushScreen();
}

void renderEbookPage() {
  if (bookReaderActive) {
    renderBookReaderPage();
    return;
  }
  if (!fileSystemReady || !LittleFS.exists(BOOK_PATH)) {
    renderMessage("电子书", "还没有电子书", "手机网页上传TXT", "支持常用中文和英文");
    return;
  }

  File file = LittleFS.open(BOOK_PATH, "r");
  size_t size = file ? file.size() : 0;
  if (file) file.close();
  String title = truncateUtf8Units(prefs.getString("bookTitle", "电子书"), 24);
  char sizeLine[28];
  snprintf(sizeLine, sizeof(sizeLine), "文本大小: %uKB", (unsigned)((size + 1023) / 1024));
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "电子书");
  drawText(12, 42, title.c_str());
  drawText(12, 62, sizeLine);
  drawText(12, 79, "OK 开始阅读");
  drawText(8, 101, "阅读中 NEXT/PREV 翻页");
  flushScreen();
}

void nextBookPage() {
  if (!bookHasNext) return;
  if ((bookPageIndex + 1) % BOOK_CHECKPOINT_PAGES == 0 &&
      !saveBookCheckpoint(bookPageIndex + 1, bookNextOffset)) {
    renderMessage("电子书", "分页索引空间不足", "请在网页清理内容", "原书仍然保留");
    return;
  }
  bookCurrentOffset = bookNextOffset;
  bookPageIndex++;
  renderBookReaderPage();
}

void previousBookPage() {
  if (bookPageIndex == 0) return;
  uint32_t offset;
  if (!findBookPageOffset(bookPageIndex - 1, offset)) return;
  bookCurrentOffset = offset;
  bookPageIndex--;
  renderBookReaderPage();
}

bool ensureWaterToday() {
  DateTimeValue now;
  if (!rtcRead(now)) return false;
  uint32_t today = dateKey(now);
  if (waterDate != today) {
    waterDate = today;
    waterCups = 0;
    prefs.putUInt("waterDate", waterDate);
    prefs.putUShort("waterCups", waterCups);
  }
  return true;
}

void renderWaterPage() {
  ensureWaterToday();
  uint32_t totalMl = (uint32_t)waterCupMl * waterCups;
  uint32_t progressRaw = waterGoalMl == 0 ? 0 : totalMl * 100UL / waterGoalMl;
  uint8_t progress = progressRaw > 100 ? 100 : (uint8_t)progressRaw;
  char totalLine[32];
  char cupsLine[32];
  snprintf(totalLine, sizeof(totalLine), "%uml / %uml", (unsigned)totalMl, waterGoalMl);
  snprintf(cupsLine, sizeof(cupsLine), "%u杯 · 每杯%uml", waterCups, waterCupMl);

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "喝水记录");
  drawText(12, 40, totalLine);
  drawText(12, 58, cupsLine);
  DrawLine(12, 67, 199, 67, COLOR_BLACK, TRUE, 1);
  DrawLine(12, 79, 199, 79, COLOR_BLACK, TRUE, 1);
  DrawLine(12, 67, 12, 79, COLOR_BLACK, TRUE, 1);
  DrawLine(199, 67, 199, 79, COLOR_BLACK, TRUE, 1);
  uint16_t fill = (uint16_t)progress * 183 / 100;
  for (uint16_t x = 0; x < fill; x++) {
    DrawLine(14 + x, 69, 14 + x, 77, COLOR_BLACK, TRUE, 1);
  }
  drawText(8, 101, progress >= 100 ? "今日已达标 · OK再记一杯" : "OK 记录喝了一杯");
  flushScreen();
}

void recordWaterCup() {
  ensureWaterToday();
  if (waterCups < 999) waterCups++;
  prefs.putUInt("waterDate", waterDate);
  prefs.putUShort("waterCups", waterCups);
  vibrateConfirm();
}

void drawMoodIcon(uint8_t index) {
  const int16_t cx = 177;
  const int16_t cy = 56;
  canvas.drawCircle(cx, cy, 23, COLOR_BLACK);
  canvas.drawCircle(cx, cy, 22, COLOR_BLACK);

  if (index == 3) {
    DrawLine(165, 49, 171, 49, COLOR_BLACK, TRUE, 1);
    DrawLine(183, 49, 189, 49, COLOR_BLACK, TRUE, 1);
  } else {
    canvas.fillCircle(168, 49, 2, COLOR_BLACK);
    canvas.fillCircle(186, 49, 2, COLOR_BLACK);
  }

  if (index == 0) {
    DrawLine(164, 61, 169, 66, COLOR_BLACK, TRUE, 1);
    DrawLine(169, 66, 177, 69, COLOR_BLACK, TRUE, 1);
    DrawLine(177, 69, 185, 66, COLOR_BLACK, TRUE, 1);
    DrawLine(185, 66, 190, 61, COLOR_BLACK, TRUE, 1);
  } else if (index == 1 || index == 2 || index == 3) {
    DrawLine(167, 65, 187, index == 1 ? 64 : 65, COLOR_BLACK, TRUE, 1);
  } else if (index == 5) {
    DrawLine(163, 44, 172, 47, COLOR_BLACK, TRUE, 1);
    DrawLine(182, 47, 191, 44, COLOR_BLACK, TRUE, 1);
    DrawLine(166, 67, 171, 63, COLOR_BLACK, TRUE, 1);
    DrawLine(171, 63, 177, 67, COLOR_BLACK, TRUE, 1);
    DrawLine(177, 67, 183, 63, COLOR_BLACK, TRUE, 1);
    DrawLine(183, 63, 188, 67, COLOR_BLACK, TRUE, 1);
  } else {
    DrawLine(165, 69, 171, 64, COLOR_BLACK, TRUE, 1);
    DrawLine(171, 64, 177, 62, COLOR_BLACK, TRUE, 1);
    DrawLine(177, 62, 183, 64, COLOR_BLACK, TRUE, 1);
    DrawLine(183, 64, 189, 69, COLOR_BLACK, TRUE, 1);
  }
}

void renderMoodPage() {
  if (moodIndex >= MOOD_COUNT) moodIndex = 2;
  const DailyItem &mood = moodItems[moodIndex];
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  drawMoodIcon(moodIndex);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, "心情指示器");
  drawText(12, 40, mood.name);
  drawText(12, 58, mood.line1);
  drawText(12, 76, mood.line2);
  drawText(8, 101, "OK 切换心情 · 手机可同步");
  flushScreen();
}

void selectNextMood() {
  moodIndex = (moodIndex + 1) % MOOD_COUNT;
  prefs.putUChar("mood", moodIndex);
  vibrateConfirm();
}

void renderMealPage() {
  DateTimeValue now;
  if (!rtcRead(now)) {
    renderMessage("三餐提醒", "RTC读取失败", "无法判断用餐时间", "请先设置时钟");
    return;
  }
  uint16_t minuteOfDay = now.hour * 60 + now.minute;
  const char *line1;
  const char *line2;
  const char *line3;
  if (minuteOfDay < 390) {
    line1 = "早饭还没到时间"; line2 = "建议时段 06:30-09:30"; line3 = "起床后记得吃早饭";
  } else if (minuteOfDay <= 570) {
    line1 = "该吃早饭了"; line2 = "建议在09:30前完成"; line3 = "别只喝咖啡";
  } else if (minuteOfDay < 690) {
    line1 = "早饭已经太晚了"; line2 = "下次记得吃早饭"; line3 = "午饭从11:30开始";
  } else if (minuteOfDay <= 840) {
    line1 = "该吃午饭了"; line2 = "建议在14:00前完成"; line3 = "给下午留足能量";
  } else if (minuteOfDay < 1050) {
    line1 = "午饭已经太晚了"; line2 = "下次记得吃午饭"; line3 = "晚饭从17:30开始";
  } else if (minuteOfDay <= 1230) {
    line1 = "该吃晚饭了"; line2 = "建议在20:30前完成"; line3 = "别拖到睡前再吃";
  } else {
    line1 = "晚饭已经太晚了"; line2 = "下次记得吃晚饭"; line3 = "明早别忘了早饭";
  }

  char title[28];
  snprintf(title, sizeof(title), "三餐提醒 %02u:%02u", now.hour, now.minute);
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, title);
  drawText(12, 40, line1);
  drawText(12, 58, line2);
  drawText(12, 76, line3);
  drawText(8, 101, "OK 刷新");
  flushScreen();
  lastMealRefreshMs = millis();
}

bool loadCurrentVocabWord() {
  if (vocabQueuePosition >= vocabQueueCount) return false;
  return readVocabWord(vocabQueue[vocabQueuePosition], vocabCurrentWord,
                       vocabCurrentMeaning);
}

void renderVocabCard() {
  if (!vocabStudyActive || !loadCurrentVocabWord()) {
    vocabStudyActive = false;
    renderMessage("背单词", "词条读取失败", "请重新全量烧录", "检查词库文件");
    return;
  }

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  char title[32];
  snprintf(title, sizeof(title), "%s %u/%u", vocabDeckName(),
           vocabQueuePosition + 1, vocabQueueCount);
  drawText(8, 16, title);

  u8g2.setFont(u8g2_font_helvB14_tf);
  if (u8g2.getUTF8Width(vocabCurrentWord.c_str()) > 196) {
    u8g2.setFont(u8g2_font_helvB10_tf);
  }
  drawText(8, 43, vocabCurrentWord.c_str());

  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  if (vocabMeaningVisible) {
    String lines[3];
    splitUtf8Lines(vocabCurrentMeaning, lines, 3, 30);
    const int16_t rows[] = {59, 73, 87};
    for (uint8_t i = 0; i < 3; i++) drawText(8, rows[i], lines[i].c_str());
    drawText(8, 101, "PREV忘记 OK模糊 NEXT认识");
  } else {
    drawText(8, 69, "先回忆中文释义");
    drawText(8, 101, "OK 显示释义 · BACK退出");
  }
  flushScreen();
}

void renderVocabPage() {
  VocabStats stats;
  if (!loadVocabIndex()) {
    renderMessage("背单词", "词库文件未安装", "需烧录完整固件包", "不能只刷OTA应用");
    return;
  }
  if (!getVocabStats(stats)) {
    renderMessage("背单词", "无法读取学习记录", "检查RTC和文件区", "网页可重置进度");
    return;
  }

  char title[32];
  char learned[32];
  char review[32];
  char plan[40];
  snprintf(title, sizeof(title), "背单词 · %s", vocabDeckName());
  snprintf(learned, sizeof(learned), "已学习 %u / %u", stats.learned, stats.total);
  snprintf(review, sizeof(review), "今日待复习 %u", stats.due);
  snprintf(plan, sizeof(plan), "今日新词 %u/%u · 共%u", vocabTodayNewDone(),
           vocabDailyNew, vocabTodayDone());

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  drawText(8, 16, title);
  drawText(12, 39, learned);
  drawText(12, 58, review);
  drawText(12, 77, plan);
  drawText(8, 101, "OK 开始学习 · 网页设置计划");
  flushScreen();
}

bool startVocabSession() {
  vocabStudyActive = false;
  vocabMeaningVisible = false;
  vocabQueueCount = 0;
  vocabQueuePosition = 0;

  uint16_t today;
  if (!getVocabToday(today) || !ensureVocabProgressFile()) return false;
  File file = LittleFS.open(vocabProgressPath(), "r");
  if (!file) return false;

  uint8_t newDone = vocabTodayNewDone();
  uint8_t newLimit = newDone < vocabDailyNew ? vocabDailyNew - newDone : 0;
  uint8_t reviewLimit = VOCAB_QUEUE_MAX - newLimit;
  for (uint16_t index = 0; index < vocabWordCount && vocabQueueCount < reviewLimit; index++) {
    VocabProgress progress;
    if (!readVocabProgress(file, index, progress)) break;
    if (progress.stage > 0 && progress.dueDay <= today) {
      vocabQueue[vocabQueueCount++] = index;
    }
  }

  uint8_t newCount = 0;
  for (uint16_t index = 0;
       index < vocabWordCount && newCount < newLimit &&
       vocabQueueCount < VOCAB_QUEUE_MAX;
       index++) {
    VocabProgress progress;
    if (!readVocabProgress(file, index, progress)) break;
    if (progress.stage == 0) {
      vocabQueue[vocabQueueCount++] = index;
      newCount++;
    }
  }
  file.close();

  if (vocabQueueCount == 0) return true;
  vocabStudyActive = true;
  return loadCurrentVocabWord();
}

void rateCurrentVocabWord(VocabRating rating) {
  if (!vocabStudyActive || vocabQueuePosition >= vocabQueueCount) return;
  uint16_t today;
  if (!getVocabToday(today) || !ensureVocabProgressFile()) {
    renderMessage("背单词", "学习记录保存失败", "请检查RTC和文件区", "BACK 返回");
    return;
  }

  File file = LittleFS.open(vocabProgressPath(), "r");
  VocabProgress progress;
  bool readOk = readVocabProgress(file, vocabQueue[vocabQueuePosition], progress);
  if (file) file.close();
  if (!readOk) {
    renderMessage("背单词", "学习记录读取失败", "网页可重置进度", "BACK 返回");
    return;
  }

  static const uint8_t intervals[] = {0, 1, 2, 4, 7, 15, 30, 60};
  uint8_t interval = 1;
  bool wasNew = progress.stage == 0;
  if (rating == VOCAB_FORGOT) {
    progress.stage = 1;
    interval = 1;
  } else if (rating == VOCAB_FUZZY) {
    if (progress.stage < 2) progress.stage = 2;
    interval = 2;
  } else {
    progress.stage = progress.stage == 0 ? 3 :
        (progress.stage < 7 ? progress.stage + 1 : 7);
    interval = intervals[progress.stage];
  }
  progress.dueDay = today + interval;
  progress.reserved = 0;
  if (!writeVocabProgress(vocabQueue[vocabQueuePosition], progress)) {
    renderMessage("背单词", "学习记录保存失败", "空间可能不足", "BACK 返回");
    return;
  }

  incrementVocabTodayDone();
  if (wasNew) incrementVocabTodayNewDone();
  vibrate(50);
  vocabQueuePosition++;
  vocabMeaningVisible = false;
  if (vocabQueuePosition >= vocabQueueCount) {
    vocabStudyActive = false;
    renderVocabPage();
    return;
  }
  renderVocabCard();
}

void handleVocabStudyButton(uint8_t pin) {
  if (pin == BUTTON_BACK_PIN) {
    vocabStudyActive = false;
    vocabMeaningVisible = false;
    renderVocabPage();
  } else if (!vocabMeaningVisible && pin == BUTTON_OK_PIN) {
    vocabMeaningVisible = true;
    renderVocabCard();
  } else if (vocabMeaningVisible) {
    if (pin == BUTTON_PREV_PIN) {
      rateCurrentVocabWord(VOCAB_FORGOT);
    } else if (pin == BUTTON_OK_PIN) {
      rateCurrentVocabWord(VOCAB_FUZZY);
    } else if (pin == BUTTON_NEXT_PIN) {
      rateCurrentVocabWord(VOCAB_KNOW);
    }
  }
}

void serviceMealReminders() {
  if (!isPageEnabled(PAGE_MEALS)) return;
  if (millis() - lastMealCheckMs < 30000UL) return;
  lastMealCheckMs = millis();

  DateTimeValue now;
  if (!rtcRead(now)) return;
  uint32_t today = dateKey(now);
  if (mealReminderDate != today) {
    mealReminderDate = today;
    mealReminderMask = 0;
    prefs.putUInt("mealDate", mealReminderDate);
    prefs.putUChar("mealMask", mealReminderMask);
  }

  uint16_t minuteOfDay = now.hour * 60 + now.minute;
  uint8_t reminderBit = 0;
  if (minuteOfDay >= 390 && minuteOfDay <= 570) {
    reminderBit = 1 << 0;
  } else if (minuteOfDay >= 690 && minuteOfDay <= 840) {
    reminderBit = 1 << 1;
  } else if (minuteOfDay >= 1050 && minuteOfDay <= 1230) {
    reminderBit = 1 << 2;
  }
  if (reminderBit == 0 || (mealReminderMask & reminderBit) != 0) return;

  mealReminderMask |= reminderBit;
  prefs.putUChar("mealMask", mealReminderMask);
  screenOff = false;
  settingTime = false;
  bookReaderActive = false;
  vocabStudyActive = false;
  currentPage = PAGE_MEALS;
  vibrateConfirm();
  renderMealPage();
}

void servicePomodoro() {
  if (!pomodoroRunning) return;
  if (pomodoroCurrentRemaining() == 0) {
    pomodoroRunning = false;
    if (pomodoroMode == POMODORO_FOCUS) {
      pomodoroCompleted++;
      prefs.putUShort("pomoDone", pomodoroCompleted);
      pomodoroMode = POMODORO_BREAK;
      pomodoroRemainingMs = POMODORO_BREAK_MS;
    } else {
      pomodoroMode = POMODORO_FOCUS;
      pomodoroRemainingMs = POMODORO_FOCUS_MS;
    }
    vibrateConfirm();
    if (currentPage == PAGE_POMODORO && !screenOff) renderPomodoroPage();
    return;
  }
  if (currentPage == PAGE_POMODORO && !screenOff &&
      millis() - pomodoroLastRenderMs >= POMODORO_REFRESH_MS) {
    renderPomodoroPage();
  }
}

void drawQrOnScreen(esp_qrcode_handle_t qrcode) {
  int size = esp_qrcode_get_size(qrcode);
  const uint8_t quiet = 4;
  const uint8_t scale = 2;
  const int16_t originX = 8;
  const int16_t originY = 24;

  for (int y = -quiet; y < size + quiet; y++) {
    for (int x = -quiet; x < size + quiet; x++) {
      uint8_t color = esp_qrcode_get_module(qrcode, x, y) ? COLOR_BLACK : COLOR_WHITE;
      int16_t px = originX + (x + quiet) * scale;
      int16_t py = originY + (y + quiet) * scale;
      for (uint8_t yy = 0; yy < scale; yy++) {
        for (uint8_t xx = 0; xx < scale; xx++) {
          DrawPixel(px + xx, py + yy, color, TRUE, 1);
        }
      }
    }
  }
}

void renderWifiPage() {
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();

  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_4x6_tf);
  drawText(8, 16, "WiFi QR");
  drawText(96, 32, "SSID:");
  drawText(96, 42, AP_SSID);
  drawText(96, 56, "PASS:");
  drawText(96, 66, AP_PASS);
  drawText(96, 82, "URL:");
  drawText(96, 92, "192.168.4.1");

  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = drawQrOnScreen;
  cfg.max_qrcode_version = 5;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  esp_qrcode_generate(&cfg, "WIFI:T:WPA;S:FortunePager;P:12345678;;");

  flushScreen();
}

void renderPage() {
  if (!isPageEnabled(currentPage)) currentPage = 0;
  if (currentPage == PAGE_WIFI) {
    renderWifiPage();
    return;
  }

  if (currentPage == PAGE_CLOCK) {
    renderClockPage();
    return;
  }

  if (currentPage == PAGE_LUCK) {
    renderLuckPage(false);
    return;
  }

  if (currentPage == PAGE_TAROT) {
    renderTarotPage(false);
    return;
  }

  if (currentPage == PAGE_FOOD) {
    renderFoodPage(false);
    return;
  }

  if (currentPage == PAGE_DRINK) {
    renderDrinkPage(false);
    return;
  }

  if (currentPage == PAGE_D6) {
    renderD6Page(false);
    return;
  }

  if (currentPage == PAGE_D20) {
    renderD20Page(false);
    return;
  }

  if (currentPage == PAGE_COIN) {
    renderCoinPage(false);
    return;
  }

  if (currentPage == PAGE_RANDOM) {
    renderRandomNumberPage(false);
    return;
  }

  if (currentPage == PAGE_KEYWORD) {
    renderKeywordPage(false);
    return;
  }

  if (currentPage == PAGE_ORACLE) {
    renderOraclePage(false);
    return;
  }

  if (currentPage == PAGE_RPS) {
    renderRpsPage(false);
    return;
  }

  if (currentPage == PAGE_GUIDANCE) {
    renderGuidancePage(false);
    return;
  }

  if (currentPage == PAGE_LUCKY_COORD) {
    renderLuckyCoordPage(false);
    return;
  }

  if (currentPage == PAGE_CUSTOM_IMAGE) {
    renderCustomImagePage();
    return;
  }

  if (currentPage == PAGE_DEVICE_INFO) {
    renderDeviceInfoPage();
    return;
  }

  if (currentPage == PAGE_GO_NOGO) {
    renderGoNoGoPage(false);
    return;
  }

  if (currentPage == PAGE_POMODORO) {
    renderPomodoroPage();
    return;
  }

  if (currentPage == PAGE_TODO) {
    renderTodoPage();
    return;
  }

  if (currentPage == PAGE_EBOOK) {
    renderEbookPage();
    return;
  }

  if (currentPage == PAGE_WATER) {
    renderWaterPage();
    return;
  }

  if (currentPage == PAGE_MOOD) {
    renderMoodPage();
    return;
  }

  if (currentPage == PAGE_MEALS) {
    renderMealPage();
    return;
  }

  if (currentPage == PAGE_VOCAB) {
    renderVocabPage();
    return;
  }

  const Page &page = pages[currentPage];
  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();

  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  drawText(8, 16, page.title);
  drawText(12, 42, page.line1);
  drawText(12, 58, page.line2);
  drawText(12, 74, page.line3);

  char footer[24];
  snprintf(footer, sizeof(footer), "%u/%u", currentPage + 1, TOTAL_PAGE_COUNT);
  drawText(88, 101, footer);

  flushScreen();
}

void renderSetTimePage() {
  char dateLine[24];
  char timeLine[24];
  char fieldLine[28];
  snprintf(dateLine, sizeof(dateLine), "%04u-%02u-%02u", editTime.year, editTime.month, editTime.day);
  snprintf(timeLine, sizeof(timeLine), "%02u:%02u", editTime.hour, editTime.minute);
  snprintf(fieldLine, sizeof(fieldLine), "正在设置: %s", fieldNames[editField]);

  Fill_Screen(COLOR_WHITE, TRUE);
  drawPageFrame();

  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COLOR_BLACK);
  u8g2.setBackgroundColor(COLOR_WHITE);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  drawText(8, 16, "设置时间");
  drawText(12, 38, fieldLine);
  drawText(12, 56, dateLine);
  drawText(12, 74, timeLine);
  drawText(8, 101, "NEXT/PREV改 OK确认 BACK取消");

  flushScreen();
}

void nextPage() {
  do {
    currentPage = (currentPage + 1) % TOTAL_PAGE_COUNT;
  } while (!isPageEnabled(currentPage));
  renderPage();
}

void prevPage() {
  do {
    currentPage = (currentPage + TOTAL_PAGE_COUNT - 1) % TOTAL_PAGE_COUNT;
  } while (!isPageEnabled(currentPage));
  renderPage();
}

void startSetTime() {
  if (!rtcRead(editTime)) {
    editTime = {2026, 1, 1, 12, 0, 0};
  }
  editField = 0;
  settingTime = true;
  renderSetTimePage();
}

void adjustField(int8_t delta) {
  switch (editField) {
    case 0:
      editTime.year = constrain((int)editTime.year + delta, 2024, 2099);
      break;
    case 1:
      editTime.month = (editTime.month + 12 + delta - 1) % 12 + 1;
      break;
    case 2: {
      uint8_t maxDay = daysInMonth(editTime.year, editTime.month);
      editTime.day = (editTime.day + maxDay + delta - 1) % maxDay + 1;
      break;
    }
    case 3:
      editTime.hour = (editTime.hour + 24 + delta) % 24;
      break;
    case 4:
      editTime.minute = (editTime.minute + 60 + delta) % 60;
      break;
  }
  clampDate(editTime);
  renderSetTimePage();
}

void confirmField() {
  if (editField < 4) {
    editField++;
    renderSetTimePage();
    return;
  }

  editTime.second = 0;
  bool ok = rtcWrite(editTime);
  if (ok) vibrateConfirm();
  settingTime = false;
  renderMessage("设置时间", ok ? "已经写入 PCF8563" : "写入失败", ok ? "秒数已归零" : "检查接线", "按任意键继续");
}

void cancelSetTime() {
  settingTime = false;
  renderPage();
}

void handleBrowseButton(uint8_t pin) {
  if (currentPage == PAGE_VOCAB && vocabStudyActive) {
    handleVocabStudyButton(pin);
    return;
  }

  if (currentPage == PAGE_EBOOK && bookReaderActive) {
    if (pin == BUTTON_NEXT_PIN) {
      nextBookPage();
    } else if (pin == BUTTON_PREV_PIN) {
      previousBookPage();
    } else if (pin == BUTTON_OK_PIN || pin == BUTTON_BACK_PIN) {
      bookReaderActive = false;
      renderEbookPage();
    }
    return;
  }

  if (pin == BUTTON_NEXT_PIN) {
    nextPage();
  } else if (pin == BUTTON_PREV_PIN) {
    prevPage();
  } else if (pin == BUTTON_OK_PIN) {
    if (currentPage == PAGE_CLOCK) {
      startSetTime();
    } else if (currentPage == PAGE_LUCK) {
      vibrateConfirm();
      renderLuckPage(true);
    } else if (currentPage == PAGE_TAROT) {
      vibrateConfirm();
      renderTarotPage(true);
    } else if (currentPage == PAGE_FOOD) {
      vibrateConfirm();
      renderFoodPage(true);
    } else if (currentPage == PAGE_DRINK) {
      vibrateConfirm();
      renderDrinkPage(true);
    } else if (currentPage == PAGE_D6) {
      vibrateConfirm();
      renderD6Page(true);
    } else if (currentPage == PAGE_D20) {
      vibrateConfirm();
      renderD20Page(true);
    } else if (currentPage == PAGE_COIN) {
      vibrateConfirm();
      renderCoinPage(true);
    } else if (currentPage == PAGE_RANDOM) {
      vibrateConfirm();
      renderRandomNumberPage(true);
    } else if (currentPage == PAGE_KEYWORD) {
      vibrateConfirm();
      renderKeywordPage(true);
    } else if (currentPage == PAGE_ORACLE) {
      vibrateConfirm();
      renderOraclePage(true);
    } else if (currentPage == PAGE_RPS) {
      vibrateConfirm();
      renderRpsPage(true);
    } else if (currentPage == PAGE_GUIDANCE) {
      vibrateConfirm();
      renderGuidancePage(true);
    } else if (currentPage == PAGE_LUCKY_COORD) {
      vibrateConfirm();
      renderLuckyCoordPage(true);
    } else if (currentPage == PAGE_CUSTOM_IMAGE) {
      renderCustomImagePage();
    } else if (currentPage == PAGE_DEVICE_INFO) {
      renderDeviceInfoPage();
    } else if (currentPage == PAGE_GO_NOGO) {
      vibrateConfirm();
      renderGoNoGoPage(true);
    } else if (currentPage == PAGE_POMODORO) {
      togglePomodoro();
      renderPomodoroPage();
    } else if (currentPage == PAGE_TODO) {
      if (completeNextTodo()) vibrateConfirm();
      renderTodoPage();
    } else if (currentPage == PAGE_EBOOK) {
      resetBookPagination();
      bookReaderActive = true;
      renderBookReaderPage();
    } else if (currentPage == PAGE_WATER) {
      recordWaterCup();
      renderWaterPage();
    } else if (currentPage == PAGE_MOOD) {
      selectNextMood();
      renderMoodPage();
    } else if (currentPage == PAGE_MEALS) {
      renderMealPage();
    } else if (currentPage == PAGE_VOCAB) {
      if (!startVocabSession()) {
        renderMessage("背单词", "无法开始学习", "检查RTC和词库文件", "网页可查看状态");
      } else if (vocabStudyActive) {
        renderVocabCard();
      } else {
        renderMessage("背单词", "今天没有待学单词", "复习任务已完成", "明天再来");
      }
    } else {
      currentPage = PAGE_LUCK;
      renderPage();
    }
  } else if (pin == BUTTON_BACK_PIN) {
    uint32_t defaultPomodoroMs = pomodoroMode == POMODORO_FOCUS
        ? POMODORO_FOCUS_MS : POMODORO_BREAK_MS;
    if (currentPage == PAGE_POMODORO &&
        (pomodoroRunning || pomodoroRemainingMs != defaultPomodoroMs)) {
      resetPomodoro();
      renderPomodoroPage();
    } else if (currentPage == 0) {
      enterScreenOff();
    } else {
      currentPage = 0;
      renderPage();
    }
  }
}

void handleSetTimeButton(uint8_t pin) {
  if (pin == BUTTON_NEXT_PIN) {
    adjustField(1);
  } else if (pin == BUTTON_PREV_PIN) {
    adjustField(-1);
  } else if (pin == BUTTON_OK_PIN) {
    confirmField();
  } else if (pin == BUTTON_BACK_PIN) {
    cancelSetTime();
  }
}

void handleWebRoot() {
  updateBatteryStatus(true);
  String html = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FortunePager</title>
<style>
:root{--bg:#eef1f4;--surface:#fff;--ink:#17201c;--muted:#66706a;--line:#d8dedb;--accent:#087f5b;--accent2:#c35b32;--soft:#e7f3ee}
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;line-height:1.4;background:var(--bg);color:var(--ink)}
main{width:min(100%,680px);margin:auto;padding:18px 16px 36px}
.appHeader{display:flex;align-items:center;justify-content:space-between;padding:6px 2px 14px}
.appHeader h1{font-size:25px;line-height:1;margin:0 0 6px;letter-spacing:0}
.appHeader p{margin:0;color:var(--muted);font-size:13px}
.deviceBadge{padding:6px 9px;border:1px solid #91b9a8;border-radius:6px;background:var(--soft);color:#086044;font-size:12px;font-weight:700}
.metrics{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:12px}
.metric{padding:10px 12px;border:1px solid var(--line);border-radius:7px;background:var(--surface)}
.metric span{display:block;color:var(--muted);font-size:11px;margin-bottom:3px}
.metric strong{font-size:14px;font-weight:650}
.tabs{position:sticky;top:0;z-index:5;display:grid;grid-template-columns:repeat(4,1fr);gap:4px;padding:7px;margin:0 0 12px;border:1px solid var(--line);border-radius:8px;background:rgba(255,255,255,.96)}
.tabs button{min-height:38px;padding:6px;border:0;border-radius:6px;background:transparent;color:var(--muted);font-weight:650}
.tabs button.active{background:var(--ink);color:#fff}
section{border:1px solid var(--line);border-radius:8px;padding:16px;margin:12px 0;background:var(--surface);box-shadow:0 2px 10px rgba(23,32,28,.04)}
section[hidden]{display:none}
h3{font-size:17px;margin:0 0 13px;letter-spacing:0}
input,button,select{font:inherit}
input[type=text],input[type=number],textarea,select{min-width:0;min-height:42px;padding:9px 10px;width:100%;border:1px solid #bcc5c0;border-radius:6px;background:#fff;color:var(--ink);outline:none}
input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(8,127,91,.12)}
textarea{min-height:90px;resize:vertical}
button{min-height:42px;padding:8px 12px;border:1px solid #b8c0bc;border-radius:6px;background:#f7f8f7;color:var(--ink);font-weight:600}
button:active{background:#e8ece9}
button.primary{background:var(--accent);color:#fff;border-color:var(--accent);width:100%;margin-top:12px}
button.primary:active{background:#066a4c}
button:disabled{opacity:.45}
.row{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-bottom:8px}
.row>label{min-width:0}
.toolbar{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:8px 0}
.stage{position:relative;width:100%;aspect-ratio:212/104;background:#dce1de;overflow:hidden;border:1px solid #aeb8b3;border-radius:6px}
canvas{display:block;width:100%;height:100%;image-rendering:pixelated;background:#fff}
#crop{touch-action:none;cursor:move}
.label{display:flex;justify-content:space-between;align-items:center;margin-top:12px;font-size:13px;color:var(--muted)}
.label strong{color:var(--ink)}
.control{display:grid;grid-template-columns:58px 1fr 45px;gap:8px;align-items:center;margin:10px 0}
.control input{width:100%;accent-color:var(--accent)}
.modes{display:grid;grid-template-columns:1fr 1fr;gap:4px;margin:8px 0}
.modes button.active{background:var(--ink);color:white;border-color:var(--ink)}
.check{display:flex;gap:8px;align-items:center;margin:10px 0}
.check input{width:19px;height:19px;accent-color:var(--accent)}
.hint,.status{font-size:13px;color:var(--muted)}
.status{min-height:19px;margin-top:8px;color:var(--accent)}
.todoItem{display:grid;grid-template-columns:30px 1fr 46px;gap:7px;align-items:center;margin:7px 0}
.todoItem input[type=checkbox]{width:20px;height:20px;accent-color:var(--accent)}
.featureList,.poolList{display:grid;grid-template-columns:1fr 1fr;gap:3px 12px;margin:8px 0 12px}
.featureItem,.poolItem{display:flex;align-items:center;gap:8px;min-height:35px;font-size:14px}
.featureItem input,.poolItem input[type=checkbox]{width:19px;height:19px;accent-color:var(--accent)}
.poolTitle{display:block;margin-top:13px;color:var(--muted);font-size:12px;text-transform:uppercase}
.poolCustom{display:grid;grid-template-columns:1fr 46px;gap:7px;margin:7px 0}
.poolAdd{display:grid;grid-template-columns:1fr 72px;gap:7px;margin-top:7px}
.fileBox{padding:13px;border:1px dashed #9eaaa4;border-radius:7px;background:#fafbfa}
.fileBox input{width:100%}
.dangerText{color:var(--accent2)}
@media(max-width:420px){main{padding:14px 12px 28px}.featureList,.poolList{grid-template-columns:1fr}.metric strong{font-size:13px}}
</style></head><body>
<main><header class="appHeader"><div><h1>FortunePager</h1><p>墨水屏随身工具</p></div><span class="deviceBadge">ESP32 在线</span></header><div class="metrics">
)HTML";

  html += "<div class='metric'><span>电池状态</span><strong>";
  html += battery.present ? String(battery.percent) + "% " + String(battery.millivolts) + "mV" : String("--");
  if (battery.charging) {
    html += " charging";
  } else if (battery.chargeComplete) {
    html += " full";
  } else if (battery.present) {
    html += " battery";
  }
  html += "</strong></div>";
  html += "<div class='metric'><span>设备信息</span><strong>v";
  html += FIRMWARE_VERSION;
  html += " · ";
  html += String(ESP.getFreeHeap() / 1024);
  html += " KB 内存可用</strong></div>";
  html += "<div class='metric'><span>Flash 容量</span><strong id='flashCapacity'>";
  html += String(ESP.getFlashChipSize() / (1024 * 1024));
  html += " MB</strong></div><div class='metric'><span>内容存储</span><strong id='storageSummary'>读取中</strong></div></div>";

  html += R"HTML(
<nav class="tabs"><button type="button" class="active" data-tab="home">概览</button><button type="button" data-tab="content">内容</button><button type="button" data-tab="device">设备</button><button type="button" data-tab="system">系统</button></nav>
<section data-panel="home">
<h3>时间</h3>
<button onclick="syncNow()">同步手机时间</button>
<form method="post" action="/set-time">
<div class="row"><input name="year" placeholder="年" value="2026"><input name="month" placeholder="月" value="7"></div>
<div class="row"><input name="day" placeholder="日" value="1"><input name="hour" placeholder="时" value="12"></div>
<input name="minute" placeholder="分" value="0">
<button type="submit">写入时间</button>
</form>
</section>
<section data-panel="device" hidden>
<h3>设备功能</h3>
<div id="featureList" class="featureList"></div>
<div class="row"><button id="featureAll" type="button">全部启用</button><button id="featureNone" type="button">全部隐藏</button></div>
<button id="featureSave" type="button" class="primary">保存设备菜单</button>
<div id="featureStatus" class="status"></div>
</section>
<section data-panel="content" hidden>
<h3>今天吃什么候选</h3>
<strong class="poolTitle">内置选项</strong><div id="foodBuiltin" class="poolList"></div>
<strong class="poolTitle">自定义选项</strong><div id="foodCustom"></div>
<div class="poolAdd"><input id="foodNew" type="text" maxlength="20" placeholder="添加食物"><button id="foodAdd" type="button">添加</button></div>
<button id="foodSave" type="button" class="primary">保存食物候选</button>
<div id="foodStatus" class="status"></div>
</section>
<section data-panel="content" hidden>
<h3>今天喝什么候选</h3>
<strong class="poolTitle">内置选项</strong><div id="drinkBuiltin" class="poolList"></div>
<strong class="poolTitle">自定义选项</strong><div id="drinkCustom"></div>
<div class="poolAdd"><input id="drinkNew" type="text" maxlength="20" placeholder="添加饮品"><button id="drinkAdd" type="button">添加</button></div>
<button id="drinkSave" type="button" class="primary">保存饮品候选</button>
<div id="drinkStatus" class="status"></div>
</section>
<section data-panel="content" hidden>
<h3>待办事项</h3>
<div id="todoList"></div>
<div class="row"><button id="todoAdd" type="button">新增待办</button><button id="todoSave" type="button">同步到设备</button></div>
<div id="todoStatus" class="status"></div>
</section>
<section data-panel="content" hidden>
<h3>电子书</h3>
<div class="fileBox"><input id="bookFile" type="file" accept=".txt,.md,.docx,text/plain,application/vnd.openxmlformats-officedocument.wordprocessingml.document"></div>
<p class="hint" id="bookLimit">TXT / Markdown / DOCX · 正在读取上传上限</p>
<button id="bookSend" type="button" class="primary" disabled>上传电子书</button>
<button id="bookDelete" type="button" class="dangerText">删除电子书</button>
<div id="bookStatus" class="status"></div>
</section>
<section data-panel="home">
<h3>背单词</h3>
<div class="row"><label>词库<select id="vocabDeck"><option value="0">六级高频</option><option value="1">雅思核心</option></select></label><label>每日新词<input id="vocabDaily" type="number" min="5" max="30" value="10"></label></div>
<div class="metrics"><div class="metric"><span>已学习</span><strong id="vocabLearned">--</strong></div><div class="metric"><span>今日待复习</span><strong id="vocabDue">--</strong></div></div>
<p id="vocabToday" class="hint"></p>
<div class="row"><button id="vocabSave" type="button">保存学习计划</button><button id="vocabReset" type="button" class="dangerText">清空当前进度</button></div>
<div id="vocabStatus" class="status"></div>
</section>
<section data-panel="content" hidden>
<h3>词库文件</h3>
<label>替换词库<select id="vocabUploadDeck"><option value="0">六级</option><option value="1">雅思</option></select></label>
<div class="fileBox"><input id="vocabFile" type="file" accept=".tsv,.txt,text/plain,text/tab-separated-values"></div>
<p id="vocabLimit" class="hint">UTF-8 TSV · 单词 / TAB / 释义</p>
<button id="vocabSend" type="button" class="primary">上传词库</button>
<div id="vocabFileStatus" class="status"></div>
</section>
<section data-panel="home">
<h3>喝水记录</h3>
<div class="row"><label>每杯（ml）<input id="waterCup" type="number" min="100" max="1000" step="10"></label><label>每日目标（ml）<input id="waterGoal" type="number" min="500" max="5000" step="100"></label></div>
<p id="waterSummary" class="hint"></p>
<div class="row"><button id="waterSave" type="button">保存设置</button><button id="waterReset" type="button">重置今日记录</button></div>
<div id="waterStatus" class="status"></div>
</section>
<section data-panel="home">
<h3>心情指示器</h3>
<select id="moodSelect"><option value="0">开心</option><option value="1">平静</option><option value="2">一般</option><option value="3">疲惫</option><option value="4">焦虑</option><option value="5">愤怒</option><option value="6">难过</option></select>
<button id="moodSave" type="button" class="primary">同步心情</button>
<div id="moodStatus" class="status"></div>
</section>
<section data-panel="system" hidden>
<h3>固件更新</h3>
<p id="storageWarning" class="dangerText"></p>
<p class="hint">上传 Arduino IDE 导出的 .bin 固件。用户名 admin，密码与设备热点密码相同。</p>
<button class="primary" type="button" onclick="location.href='/update'">打开 OTA 更新</button>
</section>
<section data-panel="content" hidden>
<h3>图片上传</h3>
<div class="fileBox"><input id="file" type="file" accept="image/*"></div>
<div class="label"><strong>裁切</strong><span>拖动图片调整位置</span></div>
<div class="stage"><canvas id="crop" width="212" height="104"></canvas></div>
<div class="control"><span>缩放</span><input id="zoom" type="range" min="100" max="400" value="100"><output id="zoomValue">100%</output></div>
<div class="toolbar">
<button id="rotate" type="button">旋转 90°</button>
<button id="reset" type="button">重新适配</button>
</div>
<div class="label"><strong>墨水屏预览</strong><span>212 × 104</span></div>
<div class="stage"><canvas id="preview" width="212" height="104"></canvas></div>
<div class="modes">
<button id="thresholdMode" type="button" class="active">清晰二值</button>
<button id="ditherMode" type="button">照片抖动</button>
</div>
<div class="control"><span>阈值</span><input id="threshold" type="range" min="40" max="220" value="150"><output id="thresholdValue">150</output></div>
<label class="check"><input id="invert" type="checkbox">黑白反色</label>
<p class="hint">图标和文字适合清晰二值；照片建议使用照片抖动。发送后会保存到设备的“我的图片”页面。</p>
<button id="send" class="primary" type="button" disabled>发送到墨水屏</button>
<div id="imageStatus" class="status"></div>
</section>
<script>
const W=212,H=104;
let storageInfo=null;
function formatBytes(bytes){
 return bytes>=1024*1024?(bytes/(1024*1024)).toFixed(2)+' MB':Math.floor(bytes/1024)+' KB';
}
async function loadStorage(){
 const response=await fetch('/status');if(!response.ok)throw new Error('无法读取存储状态');
 storageInfo=await response.json();
 document.getElementById('flashCapacity').textContent=storageInfo.flashBytes/(1024*1024)+' MB';
 document.getElementById('storageSummary').textContent=storageInfo.storageReady?
  formatBytes(storageInfo.storageFree)+' 剩余 / '+formatBytes(storageInfo.storageTotal):'存储不可用';
 document.getElementById('bookLimit').textContent='TXT / Markdown / DOCX · 本次上限 '+formatBytes(storageInfo.bookMaxBytes);
 document.getElementById('vocabLimit').textContent='UTF-8 TSV · 单词 / TAB / 释义 · 每库最多 '+storageInfo.vocabMaxWords+' 词 · 本次上限 '+formatBytes(storageInfo.vocabMaxBytes);
 document.getElementById('storageWarning').textContent=storageInfo.expansionAvailable?
  '检测到 16MB Flash，但仍使用旧分区。串口写入 16MB 扩容包后才能使用全部内容空间。':'';
 return storageInfo;
}
loadStorage().catch(error=>document.getElementById('storageSummary').textContent=error.message);
const crop=document.getElementById('crop'),cropCtx=crop.getContext('2d',{willReadFrequently:true});
const preview=document.getElementById('preview'),previewCtx=preview.getContext('2d');
const fileInput=document.getElementById('file'),zoom=document.getElementById('zoom');
const threshold=document.getElementById('threshold'),invert=document.getElementById('invert');
const zoomValue=document.getElementById('zoomValue'),thresholdValue=document.getElementById('thresholdValue');
const thresholdMode=document.getElementById('thresholdMode'),ditherMode=document.getElementById('ditherMode');
const sendButton=document.getElementById('send'),imageStatus=document.getElementById('imageStatus');
let image=null,angle=0,minScale=1,scale=1,centerX=W/2,centerY=H/2,dither=false,lastBits='';
let dragging=false,lastX=0,lastY=0,renderPending=false;

const tabButtons=document.querySelectorAll('[data-tab]');
function showTab(name){
 tabButtons.forEach(button=>button.classList.toggle('active',button.dataset.tab===name));
 document.querySelectorAll('[data-panel]').forEach(section=>section.hidden=section.dataset.panel!==name);
 window.scrollTo({top:0,behavior:'smooth'});
}
tabButtons.forEach(button=>button.addEventListener('click',()=>showTab(button.dataset.tab)));

const todoList=document.getElementById('todoList'),todoStatus=document.getElementById('todoStatus');
let todos=[];

const featureList=document.getElementById('featureList'),featureStatus=document.getElementById('featureStatus');
let features=[];
function renderFeatures(){
 featureList.replaceChildren();
 features.forEach(feature=>{
  const label=document.createElement('label');label.className='featureItem';
  const check=document.createElement('input');check.type='checkbox';check.checked=feature.enabled;check.dataset.id=feature.id;
  const name=document.createElement('span');name.textContent=feature.name;
  label.append(check,name);featureList.append(label);
 });
}
async function loadFeatures(){
 try{
  const response=await fetch('/features');if(!response.ok)throw new Error(await response.text());
  features=await response.json();renderFeatures();
 }catch(error){featureStatus.textContent='读取失败：'+error.message;}
}
document.getElementById('featureAll').addEventListener('click',()=>featureList.querySelectorAll('input').forEach(input=>input.checked=true));
document.getElementById('featureNone').addEventListener('click',()=>featureList.querySelectorAll('input').forEach(input=>input.checked=false));
document.getElementById('featureSave').addEventListener('click',async()=>{
 let mask=1;
 featureList.querySelectorAll('input:checked').forEach(input=>mask+=2**Number(input.dataset.id));
 featureStatus.textContent='正在保存…';
 try{
  const body=new URLSearchParams({mask:String(mask)});
  const response=await fetch('/features',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(!response.ok)throw new Error(await response.text());
  features=await response.json();renderFeatures();featureStatus.textContent='设备菜单已更新';
 }catch(error){featureStatus.textContent='保存失败：'+error.message;}
});
loadFeatures();

function setupOptionPool(prefix,url){
 const builtinBox=document.getElementById(prefix+'Builtin');
 const customBox=document.getElementById(prefix+'Custom');
 const newInput=document.getElementById(prefix+'New');
 const status=document.getElementById(prefix+'Status');
 let data={builtins:[],custom:[]};
 function render(){
  builtinBox.replaceChildren();customBox.replaceChildren();
  data.builtins.forEach(item=>{
   const label=document.createElement('label');label.className='poolItem';
   const check=document.createElement('input');check.type='checkbox';check.checked=item.enabled;check.dataset.id=item.id;
   const name=document.createElement('span');name.textContent=item.name;
   label.append(check,name);builtinBox.append(label);
  });
  data.custom.forEach((item,index)=>{
   const row=document.createElement('div');row.className='poolCustom';
   const input=document.createElement('input');input.type='text';input.maxLength=20;input.value=item;
   input.addEventListener('input',()=>data.custom[index]=input.value);
   const remove=document.createElement('button');remove.type='button';remove.textContent='删';
   remove.addEventListener('click',()=>{data.custom.splice(index,1);render();});
   row.append(input,remove);customBox.append(row);
  });
 }
 async function load(){
  try{
   const response=await fetch(url);if(!response.ok)throw new Error(await response.text());
   data=await response.json();render();
  }catch(error){status.textContent='读取失败：'+error.message;}
 }
 document.getElementById(prefix+'Add').addEventListener('click',()=>{
  const value=newInput.value.trim();
  if(!value)return;
  if(data.custom.length>=12){status.textContent='最多添加12个自定义选项';return;}
  data.custom.push(value);newInput.value='';render();
 });
 document.getElementById(prefix+'Save').addEventListener('click',async()=>{
  let mask=0;
  builtinBox.querySelectorAll('input:checked').forEach(input=>mask+=2**Number(input.dataset.id));
  const custom=data.custom.map(item=>item.trim()).filter(Boolean).slice(0,12);
  if(mask===0&&!custom.length){status.textContent='至少保留一个候选';return;}
  status.textContent='正在保存…';
  try{
   const body=new URLSearchParams({mask:String(mask),custom:custom.join('\n')});
   const response=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
   if(!response.ok)throw new Error(await response.text());
   data=await response.json();render();status.textContent='候选已保存，设备会重新推荐';
  }catch(error){status.textContent='保存失败：'+error.message;}
 });
 load();
}
setupOptionPool('food','/food-options');
setupOptionPool('drink','/drink-options');

function renderTodoEditor(){
 todoList.replaceChildren();
 todos.forEach((item,index)=>{
  const row=document.createElement('div');row.className='todoItem';
  const check=document.createElement('input');check.type='checkbox';check.checked=item.done;
  check.addEventListener('change',()=>item.done=check.checked);
  const input=document.createElement('input');input.type='text';input.value=item.text;input.maxLength=40;
  input.addEventListener('input',()=>item.text=input.value);
  const remove=document.createElement('button');remove.type='button';remove.textContent='删除';
  remove.addEventListener('click',()=>{todos.splice(index,1);renderTodoEditor();});
  row.append(check,input,remove);todoList.append(row);
 });
 if(!todos.length)todoStatus.textContent='暂无待办，可以先新增一项';
}
async function loadTodos(){
 try{
  const response=await fetch('/todos'),text=await response.text();
  todos=text.split(/\r?\n/).filter(Boolean).slice(0,12).map(line=>{
   const tab=line.indexOf('\t');
   return tab>=0?{done:line.slice(0,tab)==='1',text:line.slice(tab+1)}:{done:false,text:line};
  });
  renderTodoEditor();
 }catch(error){todoStatus.textContent='读取失败：'+error.message;}
}
document.getElementById('todoAdd').addEventListener('click',()=>{
 if(todos.length>=12){todoStatus.textContent='最多保存12项';return;}
 todos.push({done:false,text:''});renderTodoEditor();
 const inputs=todoList.querySelectorAll('input[type=text]');if(inputs.length)inputs[inputs.length-1].focus();
});
document.getElementById('todoSave').addEventListener('click',async()=>{
 const clean=todos.map(item=>({done:item.done,text:item.text.trim()})).filter(item=>item.text).slice(0,12);
 const body=clean.map(item=>(item.done?'1':'0')+'\t'+item.text.replace(/[\r\n\t]/g,' ')).join('\n');
 todoStatus.textContent='正在同步…';
 try{
  const response=await fetch('/todos',{method:'POST',headers:{'Content-Type':'text/plain;charset=utf-8'},body});
  if(!response.ok)throw new Error(await response.text());
  todos=clean;renderTodoEditor();todoStatus.textContent='已保存到设备';
 }catch(error){todoStatus.textContent='同步失败：'+error.message;}
});
loadTodos();

const vocabDeck=document.getElementById('vocabDeck'),vocabDaily=document.getElementById('vocabDaily');
const vocabLearned=document.getElementById('vocabLearned'),vocabDue=document.getElementById('vocabDue');
const vocabToday=document.getElementById('vocabToday'),vocabStatus=document.getElementById('vocabStatus');
async function loadVocab(){
 try{
  const response=await fetch('/vocab');if(!response.ok)throw new Error(await response.text());
  const data=await response.json();vocabDeck.value=String(data.deck);vocabDaily.value=String(data.daily);
  vocabLearned.textContent=data.installed?data.learned+' / '+data.total:'词库未安装';
  vocabDue.textContent=data.installed?String(data.due):'--';
  vocabToday.textContent=data.installed?'今天已完成 '+data.todayDone+' 个词条，其中新词 '+data.todayNew+' / '+data.daily:'需用完整烧录包写入词库文件';
 }catch(error){vocabStatus.textContent='读取失败：'+error.message;}
}
async function saveVocab(reset){
 if(reset&&!confirm('确认清空当前所选词库的全部学习进度？'))return;
 vocabStatus.textContent=reset?'正在清空…':'正在保存…';
 try{
  const body=new URLSearchParams({deck:vocabDeck.value,daily:vocabDaily.value,reset:reset?'1':'0'});
  const response=await fetch('/vocab',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(!response.ok)throw new Error(await response.text());
  vocabStatus.textContent=reset?'学习进度已清空':'学习计划已保存';await loadVocab();
 }catch(error){vocabStatus.textContent='操作失败：'+error.message;}
}
document.getElementById('vocabSave').addEventListener('click',()=>saveVocab(false));
document.getElementById('vocabReset').addEventListener('click',()=>saveVocab(true));
vocabDeck.addEventListener('change',()=>saveVocab(false));
loadVocab();

const waterCup=document.getElementById('waterCup'),waterGoal=document.getElementById('waterGoal');
const waterSummary=document.getElementById('waterSummary'),waterStatus=document.getElementById('waterStatus');
async function loadWater(){
 try{
  const response=await fetch('/water');if(!response.ok)throw new Error(await response.text());
  const data=await response.json();waterCup.value=data.cup;waterGoal.value=data.goal;
  waterSummary.textContent='今天已记录 '+data.cups+' 杯，共 '+data.total+' ml';
 }catch(error){waterStatus.textContent='读取失败：'+error.message;}
}
async function saveWater(reset){
 const body=new URLSearchParams({cup:waterCup.value,goal:waterGoal.value,reset:reset?'1':'0'});
 waterStatus.textContent='正在同步…';
 try{
  const response=await fetch('/water',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(!response.ok)throw new Error(await response.text());
  waterStatus.textContent=reset?'今日记录已重置':'设置已保存';await loadWater();
 }catch(error){waterStatus.textContent='同步失败：'+error.message;}
}
document.getElementById('waterSave').addEventListener('click',()=>saveWater(false));
document.getElementById('waterReset').addEventListener('click',()=>saveWater(true));
loadWater();

const moodSelect=document.getElementById('moodSelect'),moodStatus=document.getElementById('moodStatus');
async function loadMood(){
 try{
  const response=await fetch('/mood');if(!response.ok)throw new Error(await response.text());
  const data=await response.json();moodSelect.value=String(data.index);
 }catch(error){moodStatus.textContent='读取失败：'+error.message;}
}
document.getElementById('moodSave').addEventListener('click',async()=>{
 moodStatus.textContent='正在同步…';
 try{
  const body=new URLSearchParams({index:moodSelect.value});
  const response=await fetch('/mood',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(!response.ok)throw new Error(await response.text());
  moodStatus.textContent='心情已同步到设备';
 }catch(error){moodStatus.textContent='同步失败：'+error.message;}
});
loadMood();

const bookFile=document.getElementById('bookFile'),bookSend=document.getElementById('bookSend');
const bookStatus=document.getElementById('bookStatus');
bookFile.addEventListener('change',()=>{
 const file=bookFile.files&&bookFile.files[0];
 bookSend.disabled=!file;
 bookStatus.textContent=file?file.name+' · '+Math.ceil(file.size/1024)+'KB':'';
});

function zipU16(view,offset){return view.getUint16(offset,true)}
function zipU32(view,offset){return view.getUint32(offset,true)}
async function readDocxText(file){
 const bytes=new Uint8Array(await file.arrayBuffer()),view=new DataView(bytes.buffer);
 let eocd=-1,lower=Math.max(0,bytes.length-65557);
 for(let i=bytes.length-22;i>=lower;i--){
  if(zipU32(view,i)===0x06054b50){eocd=i;break;}
 }
 if(eocd<0)throw new Error('DOCX结构无效');
 let offset=zipU32(view,eocd+16),entries=zipU16(view,eocd+10);
 const decoder=new TextDecoder('utf-8');
 for(let n=0;n<entries;n++){
  if(zipU32(view,offset)!==0x02014b50)break;
  const method=zipU16(view,offset+10),size=zipU32(view,offset+20);
  const nameLength=zipU16(view,offset+28),extraLength=zipU16(view,offset+30),commentLength=zipU16(view,offset+32);
  const localOffset=zipU32(view,offset+42);
  const name=decoder.decode(bytes.slice(offset+46,offset+46+nameLength));
  if(name==='word/document.xml'){
   const localNameLength=zipU16(view,localOffset+26),localExtraLength=zipU16(view,localOffset+28);
   const start=localOffset+30+localNameLength+localExtraLength;
   const compressed=bytes.slice(start,start+size);
   let xmlBytes;
   if(method===0){
    xmlBytes=compressed;
   }else if(method===8&&typeof DecompressionStream!=='undefined'){
    const stream=new Blob([compressed]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
    xmlBytes=new Uint8Array(await new Response(stream).arrayBuffer());
   }else{
    throw new Error('当前浏览器不能解压DOCX，请另存为TXT');
   }
   const xml=new DOMParser().parseFromString(decoder.decode(xmlBytes),'application/xml');
   if(xml.querySelector('parsererror'))throw new Error('无法读取Word正文');
   const lines=[...xml.getElementsByTagNameNS('*','p')].map(paragraph=>
    [...paragraph.getElementsByTagNameNS('*','t')].map(node=>node.textContent||'').join('').trim()
   ).filter(Boolean);
   if(!lines.length)throw new Error('Word文档没有可读取的文字');
   return lines.join('\n');
  }
  offset+=46+nameLength+extraLength+commentLength;
 }
 throw new Error('DOCX中没有找到正文');
}

async function readBookText(file){
 const name=file.name.toLowerCase();
 if(name.endsWith('.docx'))return readDocxText(file);
 if(name.endsWith('.txt')||name.endsWith('.md'))return (await file.text()).replace(/^\uFEFF/,'');
 throw new Error('请选择TXT、Markdown或DOCX文件');
}

bookSend.addEventListener('click',async()=>{
 const file=bookFile.files&&bookFile.files[0];if(!file)return;
 bookSend.disabled=true;bookStatus.textContent='正在上传…';
 try{
  const limits=await loadStorage();
  const text=await readBookText(file);
  const textBytes=new TextEncoder().encode(text).length;
  if(textBytes===0)throw new Error('文件没有可读取的文字');
  if(textBytes>limits.bookMaxBytes)throw new Error('文本超过本次上限 '+formatBytes(limits.bookMaxBytes));
  const title=file.name.replace(/\.(txt|md|docx)$/i,'').slice(0,40)||'电子书';
  const body=new FormData();body.append('book',new Blob([text],{type:'text/plain;charset=utf-8'}),'book.txt');
  const response=await fetch('/book?title='+encodeURIComponent(title),{
   method:'POST',body
  });
  if(!response.ok)throw new Error(await response.text());
  bookStatus.textContent='上传完成，可以在设备电子书页面阅读';
  await loadStorage();
 }catch(error){bookStatus.textContent='上传失败：'+error.message;}
 finally{bookSend.disabled=false;}
});
document.getElementById('bookDelete').addEventListener('click',async()=>{
 if(!confirm('删除当前电子书并释放空间？'))return;
 try{
  const response=await fetch('/book',{method:'DELETE'});
  if(!response.ok)throw new Error(await response.text());
  bookStatus.textContent='电子书已删除';await loadStorage();
 }catch(error){bookStatus.textContent='删除失败：'+error.message;}
});
document.getElementById('vocabSend').addEventListener('click',async()=>{
 const file=document.getElementById('vocabFile').files[0];
 const status=document.getElementById('vocabFileStatus'),button=document.getElementById('vocabSend');
 if(!file){status.textContent='请先选择词库文件';return;}
 if(!confirm('替换该词库会清空该库的学习进度，另一词库不受影响。继续？'))return;
 button.disabled=true;status.textContent='正在上传…';
 try{
  const limits=await loadStorage();
  const text=(await file.text()).replace(/^\uFEFF/,'').replace(/\r\n?/g,'\n').trim();
  if(!text)throw new Error('词库为空');
  const lines=text.split('\n');
  if(lines.length>limits.vocabMaxWords)throw new Error('词条超过 '+limits.vocabMaxWords);
  if(lines.some(line=>!/^\S[^\t]*\t\s*\S.*$/.test(line)))throw new Error('每行必须包含单词、TAB 和释义');
  const body=new FormData(),blob=new Blob([text+'\n'],{type:'text/plain;charset=utf-8'});
  if(blob.size>limits.vocabMaxBytes)throw new Error('词库超过本次上限 '+formatBytes(limits.vocabMaxBytes));
  body.append('vocabulary',blob,'vocabulary.tsv');
  const deck=document.getElementById('vocabUploadDeck').value;
  const response=await fetch('/vocab-file?deck='+deck,{method:'POST',body});
  if(!response.ok)throw new Error(await response.text());
  status.textContent='词库已更新，共 '+lines.length+' 词';await loadStorage();await loadVocab();
 }catch(error){status.textContent='上传失败：'+error.message;}
 finally{button.disabled=false;}
});

function fillBlank(){
 cropCtx.fillStyle='#fff';cropCtx.fillRect(0,0,W,H);
 previewCtx.fillStyle='#fff';previewCtx.fillRect(0,0,W,H);
}
fillBlank();

function rotatedSize(){
 const swap=angle%180!==0;
 return {w:swap?image.height:image.width,h:swap?image.width:image.height};
}
function clampCenter(){
 if(!image)return;
 const r=rotatedSize(),halfW=r.w*scale/2,halfH=r.h*scale/2;
 centerX=halfW<W/2?W/2:Math.max(W-halfW,Math.min(halfW,centerX));
 centerY=halfH<H/2?H/2:Math.max(H-halfH,Math.min(halfH,centerY));
}
function fitImage(){
 if(!image)return;
 const r=rotatedSize();
 minScale=Math.max(W/r.w,H/r.h);
 scale=minScale;centerX=W/2;centerY=H/2;
 zoom.value=100;zoomValue.value='100%';
 scheduleRender();
}
function drawCrop(){
 cropCtx.fillStyle='#fff';cropCtx.fillRect(0,0,W,H);
 if(!image)return;
 cropCtx.save();
 cropCtx.translate(centerX,centerY);
 cropCtx.rotate(angle*Math.PI/180);
 cropCtx.drawImage(image,-image.width*scale/2,-image.height*scale/2,image.width*scale,image.height*scale);
 cropCtx.restore();
}
function makePreview(){
 if(!image)return;
 const src=cropCtx.getImageData(0,0,W,H),pixels=W*H,gray=new Float32Array(pixels);
 for(let i=0;i<pixels;i++){
  const p=i*4;
  gray[i]=src.data[p]*.299+src.data[p+1]*.587+src.data[p+2]*.114;
 }
 const cut=Number(threshold.value),out=previewCtx.createImageData(W,H),bits=new Array(pixels);
 for(let y=0;y<H;y++){
  for(let x=0;x<W;x++){
   const i=y*W+x,old=gray[i],value=old<cut?0:255,err=old-value;
   let black=value===0;
   if(invert.checked)black=!black;
   const shown=black?0:255,p=i*4;
   out.data[p]=shown;out.data[p+1]=shown;out.data[p+2]=shown;out.data[p+3]=255;
   bits[i]=black?'1':'0';
   if(dither){
    if(x+1<W)gray[i+1]+=err*7/16;
    if(y+1<H){
     if(x>0)gray[i+W-1]+=err*3/16;
     gray[i+W]+=err*5/16;
     if(x+1<W)gray[i+W+1]+=err/16;
    }
   }
  }
 }
 previewCtx.putImageData(out,0,0);
 lastBits=bits.join('');
}
function render(){
 renderPending=false;
 drawCrop();
 makePreview();
}
function scheduleRender(){
 if(renderPending)return;
 renderPending=true;
 requestAnimationFrame(render);
}
function canvasPoint(e){
 const r=crop.getBoundingClientRect();
 return {x:(e.clientX-r.left)*W/r.width,y:(e.clientY-r.top)*H/r.height};
}

fileInput.addEventListener('change',()=>{
 const file=fileInput.files&&fileInput.files[0];
 if(!file)return;
 const url=URL.createObjectURL(file),next=new Image();
 next.onload=()=>{
  if(image&&image._url)URL.revokeObjectURL(image._url);
  next._url=url;image=next;angle=0;fitImage();
  sendButton.disabled=false;imageStatus.textContent='图片已载入，请拖动并预览';
 };
 next.onerror=()=>{URL.revokeObjectURL(url);imageStatus.textContent='无法读取这张图片';};
 next.src=url;
});
crop.addEventListener('pointerdown',e=>{
 if(!image)return;
 dragging=true;const p=canvasPoint(e);lastX=p.x;lastY=p.y;
 crop.setPointerCapture(e.pointerId);
});
crop.addEventListener('pointermove',e=>{
 if(!dragging)return;
 const p=canvasPoint(e);centerX+=p.x-lastX;centerY+=p.y-lastY;lastX=p.x;lastY=p.y;
 clampCenter();scheduleRender();
});
crop.addEventListener('pointerup',()=>dragging=false);
crop.addEventListener('pointercancel',()=>dragging=false);
crop.addEventListener('wheel',e=>{
 if(!image)return;
 e.preventDefault();
 zoom.value=Math.max(100,Math.min(400,Number(zoom.value)+(e.deltaY<0?10:-10)));
 zoom.dispatchEvent(new Event('input'));
},{passive:false});
zoom.addEventListener('input',()=>{
 if(!image)return;
 scale=minScale*Number(zoom.value)/100;zoomValue.value=zoom.value+'%';
 clampCenter();scheduleRender();
});
document.getElementById('rotate').addEventListener('click',()=>{if(image){angle=(angle+90)%360;fitImage();}});
document.getElementById('reset').addEventListener('click',fitImage);
threshold.addEventListener('input',()=>{thresholdValue.value=threshold.value;scheduleRender();});
invert.addEventListener('change',scheduleRender);
thresholdMode.addEventListener('click',()=>{
 dither=false;thresholdMode.classList.add('active');ditherMode.classList.remove('active');scheduleRender();
});
ditherMode.addEventListener('click',()=>{
 dither=true;ditherMode.classList.add('active');thresholdMode.classList.remove('active');scheduleRender();
});
sendButton.addEventListener('click',async()=>{
 if(!image||!lastBits)return;
 sendButton.disabled=true;imageStatus.textContent='正在发送，墨水屏刷新需要几秒…';
 try{
  const response=await fetch('/draw-bw',{method:'POST',headers:{'Content-Type':'text/plain'},body:lastBits});
  if(!response.ok)throw new Error(await response.text());
   imageStatus.textContent='发送完成，图片已经保存';
 }catch(error){
  imageStatus.textContent='发送失败：'+error.message;
 }finally{
  sendButton.disabled=false;
 }
});
async function syncNow(){
 const d=new Date(),p=new URLSearchParams();
 p.set('year',d.getFullYear());p.set('month',d.getMonth()+1);p.set('day',d.getDate());
 p.set('hour',d.getHours());p.set('minute',d.getMinutes());
 await fetch('/set-time',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});
 alert('time synced');
}
</script></main></body></html>
)HTML";
  server.send(200, "text/html; charset=utf-8", html);
}

uint16_t webArgU16(const char *name, uint16_t fallback) {
  if (!server.hasArg(name)) return fallback;
  return (uint16_t)server.arg(name).toInt();
}

void handleWebSetTime() {
  DateTimeValue dt;
  dt.year = webArgU16("year", 2026);
  dt.month = (uint8_t)webArgU16("month", 1);
  dt.day = (uint8_t)webArgU16("day", 1);
  dt.hour = (uint8_t)webArgU16("hour", 12);
  dt.minute = (uint8_t)webArgU16("minute", 0);
  dt.second = 0;
  clampDate(dt);
  bool ok = rtcWrite(dt);
  if (ok) {
    vibrateConfirm();
    if (currentPage == PAGE_CLOCK) renderClockPage();
  }
  server.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "rtc write failed");
}

void handleWebGetFeatures() {
  String json = "[";
  for (uint8_t page = 1; page < PAGE_COUNT; page++) {
    if (page > 1) json += ',';
    json += "{\"id\":" + String(page);
    json += ",\"name\":\"" + String(pages[page].title) + "\"";
    json += ",\"enabled\":";
    json += isPageEnabled(page) ? "true" : "false";
    json += '}';
  }
  json += ']';
  server.send(200, "application/json; charset=utf-8", json);
}

void handleWebSetFeatures() {
  if (!server.hasArg("mask")) {
    server.send(400, "text/plain", "missing mask");
    return;
  }
  uint32_t requestedMask = strtoul(server.arg("mask").c_str(), nullptr, 10);
  enabledPageMask = (requestedMask & DEFAULT_PAGE_MASK) | 1UL;
  prefs.putUInt("pageMask", enabledPageMask);
  bookReaderActive = false;
  vocabStudyActive = false;
  if (!isPageEnabled(currentPage)) {
    currentPage = 0;
    settingTime = false;
    if (!screenOff) renderPage();
  }
  handleWebGetFeatures();
}

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char ch = value[i];
    if (ch == '\\' || ch == '"') {
      escaped += '\\';
      escaped += ch;
    } else if (ch == '\n') {
      escaped += "\\n";
    } else if (ch != '\r' && ch != '\t') {
      escaped += ch;
    }
  }
  return escaped;
}

void handleWebGetOptionPool(bool foodPool) {
  const DailyItem *builtins = foodPool ? foodItems : drinkItems;
  uint8_t builtinCount = foodPool ? FOOD_COUNT : DRINK_COUNT;
  uint32_t mask = foodPool ? enabledFoodMask : enabledDrinkMask;
  String *customItems = foodPool ? customFoodItems : customDrinkItems;
  uint8_t customCount = foodPool ? customFoodCount : customDrinkCount;

  String json = "{\"builtins\":[";
  for (uint8_t i = 0; i < builtinCount; i++) {
    if (i > 0) json += ',';
    json += "{\"id\":" + String(i);
    json += ",\"name\":\"" + jsonEscape(String(builtins[i].name)) + "\"";
    json += ",\"enabled\":";
    json += (mask & (1UL << i)) ? "true" : "false";
    json += '}';
  }
  json += "],\"custom\":[";
  for (uint8_t i = 0; i < customCount; i++) {
    if (i > 0) json += ',';
    json += '"';
    json += jsonEscape(customItems[i]);
    json += '"';
  }
  json += "]}";
  server.send(200, "application/json; charset=utf-8", json);
}

void handleWebSetOptionPool(bool foodPool) {
  if (!server.hasArg("mask") || !fileSystemReady) {
    server.send(fileSystemReady ? 400 : 500, "text/plain",
                fileSystemReady ? "missing mask" : "LittleFS unavailable");
    return;
  }

  const DailyItem *builtins = foodPool ? foodItems : drinkItems;
  uint8_t builtinCount = foodPool ? FOOD_COUNT : DRINK_COUNT;
  uint32_t validMask = (1UL << builtinCount) - 1UL;
  uint32_t newMask = strtoul(server.arg("mask").c_str(), nullptr, 10) & validMask;
  String parsedItems[CUSTOM_OPTION_MAX];
  uint8_t parsedCount = 0;
  String raw = server.hasArg("custom") ? server.arg("custom") : "";
  size_t start = 0;
  while (start <= raw.length() && parsedCount < CUSTOM_OPTION_MAX) {
    int newline = raw.indexOf('\n', start);
    size_t end = newline < 0 ? raw.length() : (size_t)newline;
    String item = raw.substring(start, end);
    item.replace("\r", "");
    item.replace("\t", " ");
    item.trim();
    item = truncateUtf8Units(item, CUSTOM_OPTION_UNITS);

    bool duplicate = item.length() == 0;
    for (uint8_t i = 0; !duplicate && i < builtinCount; i++) {
      duplicate = item == builtins[i].name;
    }
    for (uint8_t i = 0; !duplicate && i < parsedCount; i++) {
      duplicate = item == parsedItems[i];
    }
    if (!duplicate) parsedItems[parsedCount++] = item;
    if (newline < 0) break;
    start = end + 1;
  }

  if (activeOptionCount(newMask, builtinCount, parsedCount) == 0) {
    server.send(400, "text/plain", "at least one option is required");
    return;
  }

  if (foodPool) {
    if (!saveCustomOptions(CUSTOM_FOOD_PATH, parsedItems, parsedCount)) {
      server.send(500, "text/plain", "unable to save custom food");
      return;
    }
    clearCustomOptions(customFoodItems, customFoodCount);
    for (uint8_t i = 0; i < parsedCount; i++) customFoodItems[customFoodCount++] = parsedItems[i];
    enabledFoodMask = newMask;
    prefs.putUInt("foodMask", enabledFoodMask);
    prefs.remove("foodDate");
    prefs.remove("foodIdx");
    currentFoodDateKey = 0;
    foodReady = false;
    if (currentPage == PAGE_FOOD && !screenOff) renderFoodPage(false);
  } else {
    if (!saveCustomOptions(CUSTOM_DRINK_PATH, parsedItems, parsedCount)) {
      server.send(500, "text/plain", "unable to save custom drink");
      return;
    }
    clearCustomOptions(customDrinkItems, customDrinkCount);
    for (uint8_t i = 0; i < parsedCount; i++) customDrinkItems[customDrinkCount++] = parsedItems[i];
    enabledDrinkMask = newMask;
    prefs.putUInt("drinkMask", enabledDrinkMask);
    prefs.remove("drinkDate");
    prefs.remove("drinkIdx");
    currentDrinkDateKey = 0;
    drinkReady = false;
    if (currentPage == PAGE_DRINK && !screenOff) renderDrinkPage(false);
  }
  handleWebGetOptionPool(foodPool);
}

void handleWebGetFoodOptions() { handleWebGetOptionPool(true); }
void handleWebSetFoodOptions() { handleWebSetOptionPool(true); }
void handleWebGetDrinkOptions() { handleWebGetOptionPool(false); }
void handleWebSetDrinkOptions() { handleWebSetOptionPool(false); }

void handleWebGetWater() {
  ensureWaterToday();
  uint32_t totalMl = (uint32_t)waterCupMl * waterCups;
  String json = "{\"cup\":" + String(waterCupMl);
  json += ",\"goal\":" + String(waterGoalMl);
  json += ",\"cups\":" + String(waterCups);
  json += ",\"total\":" + String(totalMl) + "}";
  server.send(200, "application/json", json);
}

void handleWebSetWater() {
  long cup = server.hasArg("cup") ? server.arg("cup").toInt() : waterCupMl;
  long goal = server.hasArg("goal") ? server.arg("goal").toInt() : waterGoalMl;
  waterCupMl = (uint16_t)constrain(cup, 100L, 1000L);
  waterGoalMl = (uint16_t)constrain(goal, 500L, 5000L);
  ensureWaterToday();
  if (server.hasArg("reset") && server.arg("reset") == "1") waterCups = 0;
  prefs.putUShort("waterCup", waterCupMl);
  prefs.putUShort("waterGoal", waterGoalMl);
  prefs.putUShort("waterCups", waterCups);
  if (currentPage == PAGE_WATER && !screenOff) renderWaterPage();
  handleWebGetWater();
}

void handleWebGetMood() {
  server.send(200, "application/json", "{\"index\":" + String(moodIndex) + "}");
}

void handleWebSetMood() {
  long index = server.hasArg("index") ? server.arg("index").toInt() : moodIndex;
  moodIndex = (uint8_t)constrain(index, 0L, (long)MOOD_COUNT - 1);
  prefs.putUChar("mood", moodIndex);
  if (currentPage == PAGE_MOOD && !screenOff) renderMoodPage();
  handleWebGetMood();
}

void handleWebGetVocab() {
  bool installed = loadVocabIndex();
  VocabStats stats = {0, 0, 0};
  if (installed) installed = getVocabStats(stats);
  String json = "{\"deck\":" + String(vocabDeck);
  json += ",\"daily\":" + String(vocabDailyNew);
  json += ",\"installed\":";
  json += installed ? "true" : "false";
  json += ",\"total\":" + String(stats.total);
  json += ",\"learned\":" + String(stats.learned);
  json += ",\"due\":" + String(stats.due);
  json += ",\"todayDone\":" + String(installed ? vocabTodayDone() : 0);
  json += ",\"todayNew\":" + String(installed ? vocabTodayNewDone() : 0) + "}";
  server.send(200, "application/json", json);
}

void handleWebSetVocab() {
  uint8_t nextDeck = server.hasArg("deck") && server.arg("deck").toInt() == 1 ? 1 : 0;
  long requestedDaily = server.hasArg("daily") ? server.arg("daily").toInt() : vocabDailyNew;
  bool deckChanged = nextDeck != vocabDeck;
  vocabDeck = nextDeck;
  vocabDailyNew = (uint8_t)constrain(requestedDaily,
                                     (long)VOCAB_MIN_DAILY_NEW,
                                     (long)VOCAB_MAX_DAILY_NEW);
  prefs.putUChar("vocabDeck", vocabDeck);
  prefs.putUChar("vocabDaily", vocabDailyNew);
  if (deckChanged) {
    vocabIndexedDeck = 0xFF;
    vocabWordCount = 0;
  }
  vocabStudyActive = false;
  vocabMeaningVisible = false;

  if (server.hasArg("reset") && server.arg("reset") == "1") {
    if (!fileSystemReady) {
      server.send(500, "text/plain", "LittleFS unavailable");
      return;
    }
    LittleFS.remove(vocabProgressPath());
    prefs.remove(vocabDoneDayKey());
    prefs.remove(vocabDoneKey());
    prefs.remove(vocabNewDayKey());
    prefs.remove(vocabNewKey());
  }
  if (currentPage == PAGE_VOCAB && !screenOff) renderVocabPage();
  handleWebGetVocab();
}

void handleWebGetTodos() {
  if (!fileSystemReady || !LittleFS.exists(TODO_PATH)) {
    server.send(200, "text/plain; charset=utf-8", "");
    return;
  }
  File file = LittleFS.open(TODO_PATH, "r");
  if (!file) {
    server.send(500, "text/plain", "unable to open todo file");
    return;
  }
  server.streamFile(file, "text/plain; charset=utf-8");
  file.close();
}

void handleWebSaveTodos() {
  if (!fileSystemReady) {
    server.send(500, "text/plain", "LittleFS unavailable");
    return;
  }
  String body = server.arg("plain");
  if (body.length() > 4096) {
    server.send(413, "text/plain", "todo data too large");
    return;
  }
  File file = LittleFS.open(TODO_PATH, "w");
  if (!file) {
    server.send(500, "text/plain", "unable to save todo file");
    return;
  }
  file.write((const uint8_t *)body.c_str(), body.length());
  file.close();
  loadTodos();
  saveTodos();
  if (currentPage == PAGE_TODO && !screenOff) renderTodoPage();
  server.send(200, "text/plain", "ok");
}

void failContentUpload(const char *message) {
  if (contentUploadError.length() == 0) contentUploadError = message;
  contentUploadComplete = false;
  if (contentUploadFile) contentUploadFile.close();
  LittleFS.remove(CONTENT_UPLOAD_PATH);
}

bool validateVocabUploadLine() {
  int tab = contentUploadLine.indexOf('\t');
  String word = tab > 0 ? contentUploadLine.substring(0, tab) : "";
  String meaning = tab > 0 ? contentUploadLine.substring(tab + 1) : "";
  word.trim();
  meaning.trim();
  if (word.length() == 0 || meaning.length() == 0) {
    failContentUpload("词库格式错误：每行需为英文单词、TAB、中文释义");
    return false;
  }
  if (++contentUploadWords > vocabWordLimit()) {
    failContentUpload("词库条目超过当前分区上限");
    return false;
  }
  contentUploadLine = "";
  return true;
}

void handleContentUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (contentUploadStarted) {
      failContentUpload("一次只能上传一个文件");
      return;
    }
    contentUploadStarted = true;
    contentUploadComplete = false;
    contentUploadError = "";
    contentUploadBytes = 0;
    contentUploadWords = 0;
    contentUploadLine = "";
    contentUploadTarget = "";
    contentUploadIsVocab = server.uri() == "/vocab-file";
    if (!fileSystemReady) {
      failContentUpload("内容存储不可用");
      return;
    }
    LittleFS.remove(CONTENT_UPLOAD_PATH);
    if (upload.name != (contentUploadIsVocab ? "vocabulary" : "book")) {
      failContentUpload("上传字段不正确");
      return;
    }
    if (contentUploadIsVocab) {
      String deck = server.arg("deck");
      if (deck != "0" && deck != "1") {
        failContentUpload("请选择六级或雅思词库");
        return;
      }
      contentUploadTarget = deck == "1" ? VOCAB_IELTS_PATH : VOCAB_CET6_PATH;
    } else {
      contentUploadTarget = BOOK_PATH;
    }
    contentUploadLimit = contentUploadMax(contentUploadIsVocab);
    if (contentUploadLimit == 0) {
      failContentUpload("剩余空间不足，原文件已保留");
      return;
    }
    contentUploadFile = LittleFS.open(CONTENT_UPLOAD_PATH, "w");
    if (!contentUploadFile) failContentUpload("无法创建临时文件");
    return;
  }
  if (upload.status == UPLOAD_FILE_ABORTED) {
    failContentUpload("上传中断，原文件已保留");
    contentUploadStarted = false;
    return;
  }
  if (!contentUploadStarted || contentUploadError.length()) return;
  if (upload.status == UPLOAD_FILE_WRITE) {
    if (upload.currentSize > contentUploadLimit - contentUploadBytes) {
      failContentUpload("文件超过当前上传上限，原文件已保留");
      return;
    }
    if (contentUploadIsVocab) {
      for (size_t i = 0; i < upload.currentSize; i++) {
        char c = (char)upload.buf[i];
        if (c == '\n') {
          if (!validateVocabUploadLine()) return;
        } else if (c != '\r') {
          if (c == '\0' || contentUploadLine.length() >= 256) {
            failContentUpload("词库单行过长或含有无效字符");
            return;
          }
          contentUploadLine += c;
        }
      }
    }
    if (contentUploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
      failContentUpload("存储写入失败，原文件已保留");
      return;
    }
    contentUploadBytes += upload.currentSize;
  } else if (upload.status == UPLOAD_FILE_END) {
    if (contentUploadBytes == 0 || contentUploadBytes != upload.totalSize) {
      failContentUpload("文件为空或上传不完整");
      return;
    }
    if (contentUploadIsVocab && contentUploadLine.length() && !validateVocabUploadLine()) return;
    if (contentUploadFile) contentUploadFile.close();
    contentUploadComplete = true;
  }
}

void handleContentUploadFinished() {
  bool success = contentUploadStarted && contentUploadComplete &&
      contentUploadError.length() == 0 && contentUploadTarget.length();
  // LittleFS rename atomically replaces the old file only after a complete upload.
  if (success && !LittleFS.rename(CONTENT_UPLOAD_PATH, contentUploadTarget.c_str())) {
    failContentUpload("保存失败，原文件已保留");
    success = false;
  }
  if (success && contentUploadIsVocab) {
    bool ielts = contentUploadTarget == VOCAB_IELTS_PATH;
    LittleFS.remove(ielts ? VOCAB_IELTS_PROGRESS_PATH : VOCAB_CET6_PROGRESS_PATH);
    LittleFS.remove(ielts ? "/vocab_ielts.idx" : "/vocab_cet6.idx");
    prefs.remove(ielts ? "vDoneDayI" : "vDoneDayC");
    prefs.remove(ielts ? "vDoneI" : "vDoneC");
    prefs.remove(ielts ? "vNewDayI" : "vNewDayC");
    prefs.remove(ielts ? "vNewI" : "vNewC");
    vocabIndexedDeck = 0xFF;
    vocabWordCount = 0;
    vocabStudyActive = false;
    vocabQueueCount = 0;
    if (currentPage == PAGE_VOCAB && !screenOff) renderVocabPage();
  } else if (success) {
    String title = server.hasArg("title") ? server.arg("title") : "电子书";
    title.trim();
    if (title.length() == 0) title = "电子书";
    prefs.putString("bookTitle", truncateUtf8Units(title, 32));
    resetBookPagination();
    if (currentPage == PAGE_EBOOK && !screenOff) renderEbookPage();
  }
  if (!success) {
    if (!contentUploadError.length()) contentUploadError = "请选择文件并使用 multipart 上传";
    LittleFS.remove(CONTENT_UPLOAD_PATH);
  }
  server.send(success ? 200 : 400, "text/plain; charset=utf-8",
              success ? "ok" : contentUploadError);
  contentUploadStarted = false;
  contentUploadComplete = false;
}

void handleWebDeleteBook() {
  if (!fileSystemReady || (LittleFS.exists(BOOK_PATH) && !LittleFS.remove(BOOK_PATH))) {
    server.send(500, "text/plain", "unable to delete book");
    return;
  }
  resetBookPagination();
  prefs.remove("bookTitle");
  if (currentPage == PAGE_EBOOK && !screenOff) renderEbookPage();
  server.send(200, "text/plain", "ok");
}

bool saveCustomImage(const String &body) {
  if (!fileSystemReady) return false;

  File imageFile = LittleFS.open(CUSTOM_IMAGE_PATH, "w");
  if (!imageFile) return false;

  uint16_t written = 0;
  for (uint16_t offset = 0; offset < 212 * 104; offset += 8) {
    uint8_t packed = 0;
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (body[offset + bit] == '1') packed |= 1U << (7 - bit);
    }
    written += imageFile.write(packed);
  }
  imageFile.close();
  return written == CUSTOM_IMAGE_BYTES;
}

void handleWebDrawBw() {
  String body = server.arg("plain");
  const uint16_t expected = 212 * 104;
  if (body.length() < expected) {
    server.send(400, "text/plain", "need 212x104 bw bits");
    return;
  }

  bool saved = saveCustomImage(body);
  Fill_Screen(COLOR_WHITE, TRUE);
  for (uint16_t y = 0; y < 104; y++) {
    for (uint16_t x = 0; x < 212; x++) {
      if (body[y * 212 + x] == '1') {
        DrawPixel(x, y, COLOR_BLACK, TRUE, 1);
      }
    }
  }
  flushScreen();
  screenOff = false;
  server.send(saved ? 200 : 500, "text/plain", saved ? "ok" : "displayed but save failed");
}

bool requireOtaAuth() {
  if (server.authenticate(OTA_USER, AP_PASS)) return true;
  server.requestAuthentication(BASIC_AUTH, "FortunePager OTA", "需要 OTA 登录");
  return false;
}

void handleOtaPage() {
  if (!requireOtaAuth()) return;

  String html = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FortunePager OTA</title><style>
body{font-family:system-ui,sans-serif;margin:0;background:#f1f2ee;color:#111}
main{width:min(100%,520px);margin:auto;padding:18px}
section{border:1px solid #aaa;background:#fff;padding:16px}
input,button{font:inherit;width:100%;margin-top:12px}
button{min-height:44px;background:#111;color:#fff;border:0}
.hint{font-size:13px;color:#555}
</style></head><body><main><section><h2>Firmware OTA</h2>
<p>Current version: )HTML";
  html += FIRMWARE_VERSION;
  html += R"HTML(</p><p class="hint">选择由 Arduino IDE“导出已编译的二进制文件”生成的应用 .bin。更新期间不要断电。</p>
<form method="post" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin,application/octet-stream" required>
<button type="submit">上传并更新</button></form></section></main></body></html>)HTML";
  server.send(200, "text/html; charset=utf-8", html);
}

void failOtaUpload(const char *stage) {
  const uint8_t error = Update.getError();
  snprintf(otaUploadError, sizeof(otaUploadError),
           "OTA %s failed: %s (code=%u, heap=%lu, largest=%lu)", stage,
           error ? Update.errorString() : "No library error code available",
           error, (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
  Serial.println(otaUploadError);
  otaUploadFailed = true;
  otaUploadComplete = false;
  // Preserve the original error before releasing the interrupted update.
  if (Update.isRunning()) Update.abort();
}

void handleOtaUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaUploadAuthorized = server.authenticate(OTA_USER, AP_PASS);
    otaUploadFailed = !otaUploadAuthorized;
    otaUploadComplete = false;
    otaUploadError[0] = '\0';
    if (!otaUploadAuthorized) return;

    if (Update.isRunning()) {
      Serial.println("OTA: clearing an unfinished upload");
      Update.abort();
    }
    Serial.printf("OTA start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      failOtaUpload("begin");
    }
    return;
  }

  if (!otaUploadAuthorized || otaUploadFailed) return;

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      failOtaUpload("write");
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      failOtaUpload("end");
    } else {
      otaUploadComplete = true;
      Serial.printf("OTA complete: %u bytes\n", upload.totalSize);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    failOtaUpload("aborted");
  }
}

void handleOtaFinished() {
  if (!requireOtaAuth()) return;

  bool success = otaUploadAuthorized && otaUploadComplete && !otaUploadFailed && !Update.hasError();
  if (!success && otaUploadError[0] == '\0') {
    failOtaUpload("incomplete upload");
  }
  server.sendHeader("Connection", "close");
  server.send(success ? 200 : 500, "text/html; charset=utf-8",
              success ? "<meta charset='utf-8'><h2>更新成功，设备正在重启</h2>"
                      : String("<meta charset='utf-8'><h2>更新失败</h2><pre>") +
                            otaUploadError + "</pre><p>重新上电后可重试，不要擦除 Flash。</p>");
  if (success) {
    delay(700);
    ESP.restart();
  }
}

void handleWebStatus() {
  updateBatteryStatus(true);
  String json = "{\"version\":\"";
  json += FIRMWARE_VERSION;
  json += "\",\"heap\":" + String(ESP.getFreeHeap());
  json += ",\"uptime\":" + String(millis() / 1000);
  json += ",\"battery\":" + String(battery.present ? battery.percent : -1);
  json += ",\"batteryMillivolts\":" + String(battery.present ? battery.millivolts : 0);
  json += ",\"charging\":" + String(battery.charging ? "true" : "false");
  json += ",\"chargeComplete\":" + String(battery.chargeComplete ? "true" : "false");
  json += ",\"flashBytes\":" + String(ESP.getFlashChipSize());
  json += ",\"contentPartitionBytes\":" + String(contentPartitionBytes());
  json += ",\"storageReady\":" + String(fileSystemReady ? "true" : "false");
  json += ",\"storageTotal\":" + String(fileSystemReady ? LittleFS.totalBytes() : 0);
  json += ",\"storageUsed\":" + String(fileSystemReady ? LittleFS.usedBytes() : 0);
  json += ",\"storageFree\":" + String(contentFreeBytes());
  json += ",\"bookMaxBytes\":" + String(contentUploadMax(false));
  json += ",\"vocabMaxBytes\":" + String(contentUploadMax(true));
  json += ",\"vocabMaxWords\":" + String(vocabWordLimit());
  json += ",\"expansionAvailable\":" + String(
      ESP.getFlashChipSize() >= 16UL * 1024UL * 1024UL && !largeContentStorage() ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void startWifiConsole() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  server.on("/", HTTP_GET, handleWebRoot);
  server.on("/set-time", HTTP_POST, handleWebSetTime);
  server.on("/features", HTTP_GET, handleWebGetFeatures);
  server.on("/features", HTTP_POST, handleWebSetFeatures);
  server.on("/food-options", HTTP_GET, handleWebGetFoodOptions);
  server.on("/food-options", HTTP_POST, handleWebSetFoodOptions);
  server.on("/drink-options", HTTP_GET, handleWebGetDrinkOptions);
  server.on("/drink-options", HTTP_POST, handleWebSetDrinkOptions);
  server.on("/todos", HTTP_GET, handleWebGetTodos);
  server.on("/todos", HTTP_POST, handleWebSaveTodos);
  server.on("/book", HTTP_POST, handleContentUploadFinished, handleContentUpload);
  server.on("/book", HTTP_DELETE, handleWebDeleteBook);
  server.on("/vocab-file", HTTP_POST, handleContentUploadFinished, handleContentUpload);
  server.on("/water", HTTP_GET, handleWebGetWater);
  server.on("/water", HTTP_POST, handleWebSetWater);
  server.on("/mood", HTTP_GET, handleWebGetMood);
  server.on("/mood", HTTP_POST, handleWebSetMood);
  server.on("/vocab", HTTP_GET, handleWebGetVocab);
  server.on("/vocab", HTTP_POST, handleWebSetVocab);
  server.on("/draw-bw", HTTP_POST, handleWebDrawBw);
  server.on("/status", HTTP_GET, handleWebStatus);
  server.on("/update", HTTP_GET, handleOtaPage);
  server.on("/update", HTTP_POST, handleOtaFinished, handleOtaUpload);
  server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);
  pinMode(BUTTON_NEXT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PREV_PIN, INPUT_PULLUP);
  pinMode(BUTTON_OK_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BACK_PIN, INPUT_PULLUP);
  pinMode(BATTERY_ADC_PIN, INPUT);
  pinMode(CHARGE_ACTIVE_PIN, INPUT);
  pinMode(CHARGE_DONE_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_6db);
  attachInterrupt(digitalPinToInterrupt(BUTTON_NEXT_PIN), onNextButtonInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PREV_PIN), onPrevButtonInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_OK_PIN), onOkButtonInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_BACK_PIN), onBackButtonInterrupt, FALLING);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  i2cWrite8(PCF8563_ADDR, 0x0D, 0x00);  // Disable the unused CLKOUT output.
  prefs.begin("fortune", false);
  enabledPageMask = (prefs.getUInt("pageMask", DEFAULT_PAGE_MASK) & DEFAULT_PAGE_MASK) | 1UL;
  enabledFoodMask = prefs.getUInt("foodMask", DEFAULT_FOOD_MASK) & DEFAULT_FOOD_MASK;
  enabledDrinkMask = prefs.getUInt("drinkMask", DEFAULT_DRINK_MASK) & DEFAULT_DRINK_MASK;
  // A mount error must not silently erase books or review progress.
  fileSystemReady = LittleFS.begin(false);
  Serial.printf("LittleFS: %s\n", fileSystemReady ? "ready" : "failed");
  if (fileSystemReady) LittleFS.remove(CONTENT_UPLOAD_PATH);
  Serial.printf("Flash: %lu MB, content partition: %lu bytes\n",
                (unsigned long)ESP.getFlashChipSize() / (1024 * 1024),
                (unsigned long)contentPartitionBytes());
  loadCustomOptions(CUSTOM_FOOD_PATH, customFoodItems, customFoodCount);
  loadCustomOptions(CUSTOM_DRINK_PATH, customDrinkItems, customDrinkCount);
  loadTodos();
  resetBookPagination();
  pomodoroCompleted = prefs.getUShort("pomoDone", 0);
  waterCupMl = (uint16_t)constrain((long)prefs.getUShort("waterCup", 250), 100L, 1000L);
  waterGoalMl = (uint16_t)constrain((long)prefs.getUShort("waterGoal", 2000), 500L, 5000L);
  waterCups = prefs.getUShort("waterCups", 0);
  waterDate = prefs.getUInt("waterDate", 0);
  moodIndex = prefs.getUChar("mood", 2);
  if (moodIndex >= MOOD_COUNT) moodIndex = 2;
  mealReminderDate = prefs.getUInt("mealDate", 0);
  mealReminderMask = prefs.getUChar("mealMask", 0);
  vocabDeck = prefs.getUChar("vocabDeck", 0) == 1 ? 1 : 0;
  vocabDailyNew = (uint8_t)constrain((long)prefs.getUChar("vocabDaily", VOCAB_DEFAULT_DAILY_NEW),
                                     (long)VOCAB_MIN_DAILY_NEW,
                                     (long)VOCAB_MAX_DAILY_NEW);
  updateBatteryStatus(true);
  startWifiConsole();

  EPD_Init();
  u8g2.begin(canvas);

  renderPage();
}

void loop() {
  server.handleClient();
  servicePomodoro();
  serviceMealReminders();
  checkLowBatteryAlert();
  rearmReleasedButtons();

  uint8_t pin = takePendingButton();

  if (screenOff) {
    if (pin != 0xFF) {
      screenOff = false;
      currentPage = 0;
      renderPage();
    }
    delay(10);
    return;
  }

  if (!settingTime && currentPage == PAGE_CLOCK && millis() - lastClockRefreshMs >= CLOCK_REFRESH_MS) {
    renderClockPage();
  }

  if (!settingTime && currentPage == PAGE_MEALS &&
      millis() - lastMealRefreshMs >= 60000UL) {
    renderMealPage();
  }

  if (pin != 0xFF) {
    if (settingTime) {
      handleSetTimeButton(pin);
    } else {
      handleBrowseButton(pin);
    }
  }

  delay(10);
}
