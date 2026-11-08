#pragma once
#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>
#include <deque>
#include <string>
#include <memory>
#include <iostream>
#include "conversation_language.hpp"

namespace rkvoice {
// One bounded local catalogue. No web requests, shell commands, or account changes.
class ProductMcp {
    using J=nlohmann::json;
    J catalogue_;
    std::shared_ptr<ConversationLanguage> language_;
    std::deque<std::pair<J,J>> replies_;
    std::string current_="overview";
    std::string last_utterance_;
    bool translation_only() const {
        auto text=last_utterance_;
        for(auto& c:text)if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+'a');
        const auto has=[&](const char* s){return text.find(s)!=std::string::npos;};
        // Translating a board or explicitly showing it as well is still a display request.
        if(has(u8"牌面") || has(u8"产品牌") || has(u8"同时展示") || has(u8"并展示"))return false;
        const bool chinese=(has(u8"翻译") || has(u8"译成")) && (has(u8"把") || has(u8"将") || has(u8"这句话") || has(u8"这句"));
        return chinese || text.rfind("translate ",0)==0 || text.rfind("please translate ",0)==0;
    }
    static J result(const J& id,const J& value) { return {{"jsonrpc","2.0"},{"id",id},{"result",value}}; }
    static J error(const J& id,int code,const char* text) { return {{"jsonrpc","2.0"},{"id",id},{"error",{{"code",code},{"message",text}}}}; }
public:
    explicit ProductMcp(J data=J::object(),std::shared_ptr<ConversationLanguage> language=std::make_shared<ConversationLanguage>()):catalogue_(std::move(data)),language_(std::move(language)) {}
    void observe_language(const std::string& text) {
        last_utterance_=text;
        if(language_->observe(text))std::cout << "PRODUCT_LANGUAGE selected=" << language_->value() << " source=explicit_user_request" << std::endl;
    }
    const std::string& language() const { return language_->value(); }
    void load(const std::string& path) {
        std::ifstream in(path,std::ios::binary|std::ios::ate);
        if(!in || in.tellg()<=0 || in.tellg()>65536) return;
        in.seekg(0); auto j=J::parse(in,nullptr,false);
        if(j.is_object() && j.contains("products") && j["products"].is_object()) catalogue_=std::move(j);
    }
    bool ready() const { return catalogue_.contains("products"); }
    J handle(const J& req,bool active,const std::function<void(const J&)>& display) {
        if(!req.is_object() || !req.contains("id")) return nullptr;
        const auto id=req["id"];
        if(!(id.is_string() || id.is_number_integer()) || id.dump().size()>128) return nullptr;
        for(const auto& entry:replies_) if(entry.first==id)return entry.second;
        J response;
        try {
            if(req.value("jsonrpc","")!="2.0") response=error(id,-32600,"Invalid JSON-RPC request");
            else {
                const auto method=req.value("method","");
                if(method=="initialize") response=result(id,{{"protocolVersion","2024-11-05"},{"capabilities",{{"tools",J::object()}}},{"serverInfo",{{"name","xiaoda-khadas"},{"version","1.0.0"}}}});
                else if(method=="ping") response=result(id,J::object());
                else if(method=="tools/list") {
                    J tools=J::array();
                    if(ready()) tools.push_back({{"name","self.khadas.present"},
                        {"description",u8"Khadas产品资料查询和屏幕展示。型号语音容错（优先于泛化猜测）：Edge2 的常见转写包括 Edge 2、Edge two、Edge 二、埃吉二、艾吉二、埃奇二、艾奇二、H two、H2、H 2、H二、H Q、HQ。用户在询问Khadas产品/开发板，或正在进行Khadas产品讲解时，上述转写指向 edge2，绝不能仅因数字2而映射成mind2。例如介绍Khadas总览后说“你给我介绍一下H two吧，它有哪些接口？”应show edge2并用interfaces；随后“我说的是H two”“H2”“HQ呢？”属于型号纠正/切换请求，直接调用show edge2，不要反复声称没有H2/HQ型号。用户已经明确纠正为Edge2后，本次产品话题中后续H2/HQ继续按Edge2理解。没有Khadas/开发板/产品讲解语境的孤立H2或HQ先简短确认；化学氢气H2、公司总部HQ、其他品牌明确的H2型号、单词翻译、拼写或读音问题均不得据此展示。拒绝展示优先。不要把字母H、E、爱、2等单个短音当作Edge2。语音品牌容错：在明确要求介绍、展示、推荐或比较产品/设备的语境中，将 Khadas 的常见近音转写（不区分英文大小写）：Kadas、Khad as、Cadas、Cads、Kads、CAS、KAS、卡达斯、卡达思、卡达兹、喀达斯、哈达斯、卡斯，理解为 Khadas。例如“介绍一下cas产品”“给我看看cads的产品”“推荐卡达斯开发板”均直接调用本工具；只说“介绍卡达斯”也可展示overview。已在讲解Khadas时，近音品牌词和“它/这一款”的追问延续当前产品。没有具体型号就展示overview；仅按上面明确列出的型号容错结合语境匹配，不随意猜型号。无需纠正用户发音或提及转写错误。容错有边界：否定/拒绝展示优先，例如“不要介绍CAS产品”不得show；翻译、拼写、读音、化学CAS编号、计算机CAS操作或明确其他品牌的问题不得据此展示。仅孤立说CAS/卡斯而无产品语境时先简短确认，不能任何包含cas的单词都匹配；未列出的近音只有结合产品语境能明确指向Khadas时才采用，不确定时确认。结束产品展示不结束聊天，也不改变语言约定。用户询问Khadas/哈达斯产品、Edge2/VIM4开发板、Mind Pro/Mind迷你电脑、Graphics显卡扩展、xPlay便携显示器、Tea Pro耳放，或要求介绍、推荐、比较、看接口时，先调用再根据返回的官方资料回答。本工具不用于天气、翻译或普通聊天；但这些请求仍须正常回应：需要翻译时直接翻译用户指定的文字，需要天气时使用天气查询，不要拒绝任务，也不要反问是否改看产品。翻译内容中有品牌名或“介绍产品”不代表用户要求实际展示。含近音品牌的中文提问仍按当前语言约定回答，不得因识别成英文字母而切换英文或双语。 默认使用中文，英文品牌名/型号不是切换语言的指令。只有用户明确要求时切换英文或双语，并延续到下一次明确切换。工具结果的reply_language是本轮产品讲解语言，语音与牌面须一致。 product: overview=产品总览, edge2, vim4, mind2=Mind 2/2s, tea_pro, mind_pro=Mind Pro, graphics=Mind Graphics RTX4060Ti, graphics2=Mind Graphics 2 RTX5060Ti, xplay=Mind xPlay portable display. 型号含糊时先确认，不要随意凑一个型号做比较。 current means the last displayed product. action show/compare/hide; hide用于用户说收起来、结束产品介绍、回到聊天。回答简洁，未提供的规格不要猜，价格以官网为准。"},
                        {"inputSchema",{{"type","object"},{"properties",{
                            {"action",{{"type","string"},{"enum",{"show","compare","hide"}}}},
                            {"product",{{"type","string"},{"description","overview, edge2, vim4, mind2, tea_pro, mind_pro, graphics, graphics2, xplay, current"}}},
                            {"second",{{"type","string"},{"description","Second product ID for compare; empty otherwise"}}},
                            {"language",{{"type","string"},{"enum",{"zh","en","bilingual"}},{"description",u8"默认zh；仅按用户明确的语言切换请求选择en或bilingual。设备将按实际转写校验，最终以结果reply_language为准。"}}},
                            {"view",{{"type","string"},{"enum",{"features","interfaces"}}}}
                        }},{"required",{"action","product","language"}},{"additionalProperties",false}}}});
                    response=result(id,{{"tools",tools}});
                } else if(method=="tools/call") {
                    const auto p=req.value("params",J::object());
                    if(p.value("name","")!="self.khadas.present") response=error(id,-32602,"Unknown tool");
                    else if(!ready()) response=error(id,-32603,"Product catalogue unavailable");
                    else {
                        const auto a=p.at("arguments");
                        std::string action=a.value("action","show"),product=a.value("product","current"),second=a.value("second",""),lang=a.value("language","zh"),view=a.value("view","features");
                        if(product=="current")product=current_;
                        if((action!="show" && action!="compare" && action!="hide") ||
                           (lang!="zh" && lang!="en" && lang!="bilingual") ||
                           (view!="features" && view!="interfaces") ||
                           !catalogue_["products"].contains(product) ||
                           (action=="compare" && (second==product || second=="overview" || product=="overview" || !catalogue_["products"].contains(second)))) {
                            response=error(id,-32602,"Invalid product or display option; use the documented IDs");
                        } else if(!active) {
                            response=result(id,{{"isError",true},{"content",J::array({{{"type","text"},{"text","Turn ended or interrupted; no display change was made."}}})}});
                        } else if(action!="hide" && translation_only()) {
                            std::cout << "PRODUCT_VIEW_SKIPPED reason=translation_only" << std::endl;
                            response=result(id,{{"isError",true},{"content",J::array({{{"type","text"},{"text",u8"未展示产品牌：用户是在要求翻译文字，不是在请求产品介绍。请直接翻译用户指定的原文，不要补充产品资料，不要再次调用展示工具，不要声称已展示。Translation-only request: translate the supplied phrase into the requested language; do not show products or retry this tool."}}})}});
                        } else {
                            if(lang!=language_->value())std::cout << "PRODUCT_LANGUAGE corrected=" << lang << " effective=" << language_->value() << std::endl;
                            lang=language_->value();
                            const J event={{"action",action},{"product",product},{"second",action=="compare"?second:""},{"language",lang},{"view",view}};
                            J facts={{"display",event},{"reply_language",lang},{"verified",catalogue_.value("verified","")},{"guidance",u8"使用这些官方事实回答，保持当前聊天语言或双语约定。首轮用2至3句解释产品用途及关键特点，追问再展开。价格、库存、未列出的性能或兼容性未知，不要编造。介绍完可以继续聊天。"}};
                            facts["language_instruction"]=lang=="en" ? "Respond in English. Keep this language for follow-up product questions until the user explicitly switches. Product names do not change it." : lang=="bilingual" ? "Respond briefly in Chinese, then English. Preserve this bilingual preference on follow-ups and closing the product board." : u8"本轮必须用中文讲解；品牌名、型号和英文规格可保留。不得因为Khadas、Mind等英文名称而整段改用英文。用户未明确要求切换时，后续追问继续中文。";
                            if(action!="hide") {
                                facts["product"]=catalogue_["products"][product];
                                if(action=="compare")facts["second_product"]=catalogue_["products"][second];
                                if(product=="overview")facts["available_products"]=catalogue_["products"];
                                current_=product;
                            }
                            display(event);
                            const std::string responseRule=lang=="en"
                                ? "ACTIVE SPEAKING LANGUAGE: ENGLISH. Give the spoken answer in English only, even when the current question or source facts are Chinese. The user has not explicitly switched away from English. If unclear, ask a short clarification in English. Do not treat product names or the source language as a request to switch."
                                : lang=="bilingual" ? "ACTIVE SPEAKING LANGUAGE: BILINGUAL. Give a short Chinese answer followed by the equivalent English answer. Preserve BOTH languages on follow-ups until the user explicitly switches."
                                : u8"当前口头回答语言：中文。请直接用中文回答本轮问题，必要的英文品牌名和型号可以保留。用户没有明确要求切换，不得因为资料、品牌名或工具字段含英文而整段改说英文。不清楚时也用中文简短询问。";
                            response=result(id,{{"isError",false},{"content",J::array({{{"type","text"},{"text",facts.dump()}},{{"type","text"},{"text",responseRule}}})}});
                        }
                    }
                } else response=error(id,-32601,"Method not found");
            }
        } catch(...) { response=error(id,-32602,"Malformed parameters"); }
        replies_.push_back({id,response}); if(replies_.size()>64)replies_.pop_front();
        return response;
    }
};
}
