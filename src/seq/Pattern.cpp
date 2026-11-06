#include "Pattern.h"
#include <cctype>
#include <sstream>

namespace acid3
{
// Token per step: <gate><note><octave>[a][s]
//   gate: 'n' note, 't' tie, 'r' rest;  note: 0..12;  octave: '-' or '+' (optional)
//   e.g. "n3+as" = note D#, octave up, accent, slide.   Tokens are space separated.
std::string Pattern::toString() const
{
    std::ostringstream os;
    for (int i = 0; i < length; ++i)
    {
        const auto& s = steps[(size_t) i];
        os << (s.gate == Gate::Note ? 'n' : s.gate == Gate::Tie ? 't' : 'r') << s.note
           << (s.octave < 0 ? "-" : s.octave > 0 ? "+" : "");
        if (s.accent) os << 'a';
        if (s.slide) os << 's';
        if (i + 1 < length) os << ' ';
    }
    return os.str();
}

bool Pattern::fromString (const std::string& text, Pattern& out)
{
    std::istringstream is (text);
    std::string tok;
    Pattern p;
    int n = 0;
    while (is >> tok)
    {
        if (n >= maxSteps || tok.size() < 2) return false;
        Step s;
        switch (tok[0])
        {
            case 'n': s.gate = Gate::Note; break;
            case 't': s.gate = Gate::Tie; break;
            case 'r': s.gate = Gate::Rest; break;
            default: return false;
        }
        size_t i = 1;
        int note = 0;
        while (i < tok.size() && std::isdigit ((unsigned char) tok[i]))
            note = note * 10 + (tok[i++] - '0');
        if (note > 12) return false;
        s.note = note;
        if (i < tok.size() && (tok[i] == '-' || tok[i] == '+'))
            s.octave = tok[i++] == '-' ? -1 : 1;
        for (; i < tok.size(); ++i)
        {
            if (tok[i] == 'a') s.accent = true;
            else if (tok[i] == 's') s.slide = true;
            else return false;
        }
        p.steps[(size_t) n++] = s;
    }
    if (n == 0) return false;
    p.length = n;
    out = p;
    return true;
}
} // namespace acid3
