#include "../src/product_mcp.hpp"
#include <iostream>
#include <cassert>
using J=nlohmann::json;
int main(int argc,char**argv) {
 assert(argc==2);rkvoice::ProductMcp m;m.load(argv[1]);assert(m.ready());
 int events=0;J last;auto emit=[&](const J& j){++events;last=j;};
 auto ask=[&](int id,const std::string& method,const J& params,bool active=true){return m.handle({{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}},active,emit);};
 assert(ask(1,"initialize",J::object())["result"]["capabilities"].contains("tools"));
 assert(ask(2,"tools/list",J::object())["result"]["tools"].size()==1);
 J args={{"action","show"},{"product","edge2"},{"language","bilingual"}};
 J p={{"name","self.khadas.present"},{"arguments",args}};
 auto res=ask(3,"tools/call",p);assert(!res["result"]["isError"].get<bool>() && events==1);
 assert(res.dump().find("RK3588S2")!=std::string::npos);
 ask(3,"tools/call",p);assert(events==1); // at-most-once on retries
 ask(4,"tools/call",p,false);assert(events==1); // interrupted/late calls
 p["arguments"]["product"]="../../etc/passwd";assert(ask(5,"tools/call",p).contains("error"));assert(events==1);
 p["arguments"]["product"]=123;assert(ask(6,"tools/call",p).contains("error"));
 p["arguments"]["product"]="current";p["arguments"]["view"]="interfaces";
 ask(7,"tools/call",p);assert(events==2 && last["product"]=="edge2" && last["view"]=="interfaces");
 p["arguments"]["action"]="compare";p["arguments"]["second"]="vim4";ask(8,"tools/call",p);assert(events==3 && last["second"]=="vim4");
 p["arguments"]["second"]="edge2";assert(ask(9,"tools/call",p).contains("error"));assert(events==3);
 p["arguments"]["action"]="hide";ask(10,"tools/call",p);assert(events==4 && last["action"]=="hide");
 assert(m.handle({{"jsonrpc","2.0"},{"method","notifications/initialized"}},true,emit).is_null());
 for(const auto& id: {"mind_pro","graphics","graphics2","xplay"}) {
  static int request=20;p["arguments"]={{"action","show"},{"product",id},{"language","en"},{"view","interfaces"}};
  auto added=ask(request++,"tools/call",p);assert(added.contains("result") && !added["result"]["isError"].get<bool>() && last["product"]==id);
  if(std::string(id)=="xplay")assert(added.dump().find("touchscreen")!=std::string::npos);
  if(std::string(id)=="graphics2")assert(added.dump().find("5060")!=std::string::npos);
  if(std::string(id)=="graphics")assert(added.dump().find("4060")!=std::string::npos);
 }
 p["arguments"]={{"action","compare"},{"product","graphics"},{"second","graphics2"},{"language","bilingual"},{"view","features"}};
 auto generations=ask(30,"tools/call",p);assert(generations.dump().find("4060")!=std::string::npos && generations.dump().find("5060")!=std::string::npos);
 p["arguments"]={{"action","show"},{"product","overview"},{"language","zh"}};
 assert(ask(31,"tools/call",p).dump().size()<65536);

 // Regression: a Chinese request with a Latin brand must not authorize English.
 auto language=std::make_shared<rkvoice::ConversationLanguage>();
 rkvoice::ProductMcp guarded(J::object(),language);guarded.load(argv[1]);
 int request=100;J languageEvent;
 auto callLanguage=[&](const std::string& claimed){return guarded.handle({{"jsonrpc","2.0"},{"id",request++},{"method","tools/call"},{"params",{{"name","self.khadas.present"},{"arguments",{{"action","show"},{"product","overview"},{"language",claimed}}}}}},true,[&](const J& e){languageEvent=e;});};
 guarded.observe_language(u8"介绍下cads产品。");
 auto corrected=callLanguage("en");assert(languageEvent["language"]=="zh");
 assert(J::parse(corrected["result"]["content"][0]["text"].get<std::string>())["reply_language"]=="zh");
 for(const std::string text:{u8"为什么你用英文回答？",u8"不要用英文介绍",u8"把‘请用英文回答’翻译成中文",u8"Mind Pro 的英文名字是什么？"}) {guarded.observe_language(text);assert(guarded.language()=="zh");}
 guarded.observe_language(u8"接下来请用英文介绍这些产品。");callLanguage("zh");assert(languageEvent["language"]=="en");
 guarded.observe_language(u8"它有哪些接口？");callLanguage("zh");assert(languageEvent["language"]=="en");
 rkvoice::ProductMcp reconnected(J::object(),language);assert(reconnected.language()=="en");
 guarded.observe_language(u8"请用中英双语介绍。");callLanguage("en");assert(languageEvent["language"]=="bilingual");
 guarded.observe_language(u8"收起来，今天天气怎么样？");assert(guarded.language()=="bilingual");
 guarded.observe_language(u8"现在切回中文。");callLanguage("en");assert(languageEvent["language"]=="zh");
 guarded.observe_language("Please introduce Khadas in English.");assert(guarded.language()=="en");
 guarded.observe_language("Switch back to Chinese.");assert(guarded.language()=="zh");
 guarded.observe_language("Please use both Chinese and English.");assert(guarded.language()=="bilingual");
 guarded.observe_language(u8"中文。");assert(guarded.language()=="zh");
 guarded.observe_language(u8"能不能用英文讲一下这个产品？");assert(guarded.language()=="en");
 guarded.observe_language(u8"先中文后英文介绍一下。");assert(guarded.language()=="bilingual");
 guarded.observe_language("Could you please answer in Chinese?");assert(guarded.language()=="zh");

 // Regression from the actual session: English -> explicit Chinese with modifiers.
 const std::vector<std::string> backToChinese={
  u8"还是用中文一句话跟我说吧。",u8"用中文简单一点给我介绍。",u8"请用中文给我一句话介绍。",
  u8"现在用中文简短回答就好。",u8"中文就好。",u8"中文就行。",u8"还是用中文两句话解释一下。"
 };
 for(const auto& phrase:backToChinese){
  guarded.observe_language(u8"请用英文介绍。");
  guarded.observe_language(phrase);assert(guarded.language()=="zh");
  const auto result=callLanguage("en");assert(languageEvent["language"]=="zh");
  assert(J::parse(result["result"]["content"][0]["text"].get<std::string>())["reply_language"]=="zh");
 }
 for(const std::string phrase:{u8"不要用中文。",u8"中文一句话怎么翻译成英文？",u8"把‘还是用中文一句话跟我说吧’翻译成英文。",u8"为什么刚才用中文？",u8"这个产品的中文名字是什么？",u8"介绍一下cas产品。",u8"收起来吧。"}){
  guarded.observe_language(u8"请用英文介绍。");guarded.observe_language(phrase);assert(guarded.language()=="en");
 }
 guarded.observe_language(u8"请用中英双语简单介绍一下。");assert(guarded.language()=="bilingual");
 guarded.observe_language(u8"它有哪些接口？");assert(guarded.language()=="bilingual");
 guarded.observe_language("Please introduce Mind to me using Chinese.");assert(guarded.language()=="zh");
 guarded.observe_language("Please introduce Mind to me use English.");assert(guarded.language()=="en");
 std::cout<<"LANGUAGE_MODIFIER_REGRESSION_OK actual_phrase product_reply_language quote_negation sticky_followup bilingual\n";
 std::cout<<"PRODUCT_LANGUAGE_TEST_OK chinese_brand_default explicit_switch sticky_followup bilingual quote_negation reconnect\n";

 // A mistranscribed brand inside a translation must never open the board,
 // even if the cloud mistakenly requests show/compare. A later real request recovers.
 rkvoice::ProductMcp intent;intent.load(argv[1]);int shown=0;int guardId=1000;
 auto guardedCall=[&](const std::string& action){return intent.handle({{"jsonrpc","2.0"},{"id",guardId++},{"method","tools/call"},{"params",{{"name","self.khadas.present"},{"arguments",{{"action",action},{"product","edge2"},{"second","vim4"},{"language","zh"}}}}}},true,[&](const J&){++shown;});};
 for(const std::string t:{u8"把介绍卡达斯产品翻译成英文，不要阐释。",u8"将介绍CAS产品译成英文。","Please translate introduce CAS products into Chinese."}) {
  intent.observe_language(t);assert(guardedCall("show")["result"]["isError"]==true);assert(guardedCall("compare")["result"]["isError"]==true);assert(shown==0);
 }
 assert(guardedCall("hide")["result"]["isError"]==false && shown==1);
 for(const std::string t:{u8"介绍一下CAS产品。",u8"请用英文介绍卡达斯产品。",u8"把产品牌翻译成英文。",u8"把介绍翻译成英文并展示产品。"}) {
  intent.observe_language(t);assert(guardedCall("show")["result"]["isError"]==false);
 }
 assert(shown==5);
 std::cout<<"PRODUCT_TRANSLATION_GUARD_OK show_compare_blocked hide_allowed later_request_recovers\n";
 std::cout<<"PRODUCT_PROTOCOL_TEST_OK expanded=8 graphics_generations=xplay_host_required catalogue_valid compare bilingual close malformed retry interrupted\n";
}
