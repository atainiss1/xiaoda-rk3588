#pragma once
#include <string>
#include <regex>
#include <algorithm>
#include <vector>

namespace rkvoice {
// Only explicit language directives change this state. Product names, quoted
// text, translation exercises and ordinary follow-ups do not select a language.
class ConversationLanguage {
    std::string value_="zh";
public:
    const std::string& value() const { return value_; }
    bool observe(std::string text) {
        if(text.size()>8192)return false;
        const auto before=value_;
        for(const auto& quote:std::vector<std::pair<std::string,std::string>>{{"\"","\""},{"“","”"},{"‘","’"},{"「","」"},{"『","』"}}) {
            std::size_t p=0;
            while((p=text.find(quote.first,p))!=std::string::npos) {
                const auto end=text.find(quote.second,p+quote.first.size());
                text.replace(p,end==std::string::npos?text.size()-p:end+quote.second.size()-p," ");
            }
        }
        for(char& c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
        for(const std::string sep:{"。","，","！","？","；",",","!","?",";","\n"}) {
            std::size_t p=0;while((p=text.find(sep,p))!=std::string::npos){text.replace(p,sep.size(),".");++p;}
        }
        static const std::regex chinese(u8"^(?:(?:请你|请|我想让你|我想|帮我|给我|能不能|可不可以|能否|可以|能|麻烦你|麻烦|小哒|你好小哒|你|接下来|以后|后面|这次|现在|从现在开始|那就|还是|继续|我们|咱们|一直|都|再|把回答语言|把语言|回答语言|语言|改为|改成|换成|切换成|切换到|切换为|改用|换用|改回|换回|切回|说|用|使用) *)*(先中文后英文|先说中文再说英文|先中文再英文|中英文|中英双语|中英文双语|中文和英文|中文加英文|英文和中文|双语|英语|英文|中文|汉语|普通话)(?:$| *(?:(?:简单一点|简短一点|详细一点|简单|简短|简洁|详细|自然|正常|直接|一句话|两句话|三句话|几句话|一两句话|[0-9]+句话|给我|跟我|和我|对我|为我|帮我|只|仅|就|再|先) *)*(?:介绍|回答|回复|解释|讲解|讲|说|聊|对话|交流|沟通|模式|来|吧|呀|好吗|可以吗|好不好|好|行).*)");
        static const std::regex english("^(?:(?:can you|could you|would you|please) +)*(?:(?:now|from now on) +)?(?:(?:speak|reply|respond|answer|talk|chat|continue)(?: +to me)?(?: +only)?(?: +in)?|switch(?: +back)? +to|use) +(english|chinese|mandarin|both english and chinese|both chinese and english)(?:$| +.*)");
        static const std::regex englishEnd("^(?:(?:can you|could you|would you|please) +)*(?:introduce|explain|describe|compare|tell me|answer|reply|respond) +.* +(?:in|using|use) +(english|chinese|mandarin|both english and chinese|both chinese and english)(?: +please)?$");
        static const std::regex shortEnglish("^(english|chinese|mandarin|both english and chinese|both chinese and english)(?: +please)?$");
        std::size_t start=0;
        while(start<=text.size()) {
            const auto end=text.find('.',start);
            auto clause=text.substr(start,end==std::string::npos?std::string::npos:end-start);
            auto first=clause.find_first_not_of(" \t\r");
            if(first!=std::string::npos)clause=clause.substr(first,clause.find_last_not_of(" \t\r")-first+1);
            // Do not interpret embedded instructions in translation or examples.
            const bool quotedTask=clause.find(u8"翻译")!=std::string::npos || clause.find("translate")!=std::string::npos || clause.find(u8"例如")!=std::string::npos || clause.find("for example")!=std::string::npos;
            std::smatch match;
            if(!quotedTask && (std::regex_match(clause,match,chinese) || std::regex_match(clause,match,english) || std::regex_match(clause,match,englishEnd) || std::regex_match(clause,match,shortEnglish))) {
                const std::string language=match[1];
                value_=language==u8"中英文" || language.find(u8"先")!=std::string::npos || language.find(u8"双语")!=std::string::npos || language.find(u8"和")!=std::string::npos || language.find(u8"加")!=std::string::npos || language.find("both")!=std::string::npos ? "bilingual" :
                    (language==u8"英语" || language==u8"英文" || language=="english" ? "en" : "zh");
            }
            if(end==std::string::npos)break;
            start=end+1;
        }
        return before!=value_;
    }
};
}
