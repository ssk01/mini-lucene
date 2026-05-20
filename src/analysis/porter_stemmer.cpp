#include "minilucene/analysis/porter_stemmer.h"

namespace minilucene {
namespace analysis {

bool PorterStemmer::Cons(int i) const {
    switch ((*word_)[i]) {
        case 'a': case 'e': case 'i': case 'o': case 'u': return false;
        case 'y': return i == 0 ? true : !Cons(i - 1);
        default: return true;
    }
}

int PorterStemmer::M() const {
    int n = 0, i = 0;
    while (true) {
        if (i > j_) return n;
        if (!Cons(i)) break;
        ++i;
    }
    ++i;
    while (true) {
        while (true) {
            if (i > j_) return n;
            if (Cons(i)) break;
            ++i;
        }
        ++i; ++n;
        while (true) {
            if (i > j_) return n;
            if (!Cons(i)) break;
            ++i;
        }
        ++i;
    }
}

bool PorterStemmer::VowelInStem() const {
    for (int i = 0; i <= j_; ++i) {
        if (!Cons(i)) return true;
    }
    return false;
}

bool PorterStemmer::DoubleCons(int i) const {
    if (i < 1) return false;
    return (*word_)[i] == (*word_)[i - 1] && Cons(i);
}

bool PorterStemmer::LiEnding(int i) const {
    if (i < 1) return false;
    switch ((*word_)[i]) {
        case 'l': case 's': case 'z': return true;
        default: return false;
    }
}

bool PorterStemmer::Cvc(int i) const {
    if (i < 2 || !Cons(i) || Cons(i - 1) || !Cons(i - 2)) return false;
    char c = (*word_)[i];
    return c != 'w' && c != 'x' && c != 'y';
}

bool PorterStemmer::Ends(const std::string& s) {
    int sz = static_cast<int>(s.size());
    int o = k_ - sz + 1;
    if (o < 0) return false;
    for (int i = 0; i < sz; ++i) {
        if ((*word_)[o + i] != s[i]) return false;
    }
    j_ = k_ - sz;
    return true;
}



void PorterStemmer::SetTo(const std::string& s) {
    int len = static_cast<int>(s.size());
    int o = j_ + 1;
    word_->erase(o);
    word_->insert(o, s);
    k_ = static_cast<int>(word_->size()) - 1;
}

void PorterStemmer::Replace(const std::string& s) {
    if (M() > 0) SetTo(s);
}

void PorterStemmer::Step1() {
    if ((*word_)[k_] == 's') {
        if (Ends("sses")) k_ -= 2;
        else if (Ends("ies")) SetTo("i");
        else if ((*word_)[k_ - 1] != 's') k_--;
    }
    if (Ends("eed")) { if (M() > 0) k_--; }
    else if ((Ends("ed") || Ends("ing")) && VowelInStem()) {
        k_ = j_;
        if (Ends("at")) SetTo("ate");
        else if (Ends("bl")) SetTo("ble");
        else if (Ends("iz")) SetTo("ize");
        else if (DoubleCons(k_)) {
            k_--;
            char c = (*word_)[k_];
            if (c == 'l' || c == 's' || c == 'z') k_++;
        } else if (M() == 1 && Cvc(k_)) SetTo("e");
    }
    if (Ends("y") && VowelInStem()) {
        (*word_)[k_] = 'i';
    }
}

void PorterStemmer::Step2() {
    j_ = k_;
    switch ((*word_)[k_ - 1]) {
        case 'a': if (Ends("ational")) { Replace("ate"); break; }
                  if (Ends("tional")) { Replace("tion"); break; } break;
        case 'c': if (Ends("enci")) { Replace("ence"); break; }
                  if (Ends("anci")) { Replace("ance"); break; } break;
        case 'e': if (Ends("izer")) { Replace("ize"); break; } break;
        case 'l': if (Ends("bli")) { Replace("ble"); break; }
                  if (Ends("alli")) { Replace("al"); break; }
                  if (Ends("entli")) { Replace("ent"); break; }
                  if (Ends("eli")) { Replace("e"); break; }
                  if (Ends("ousli")) { Replace("ous"); break; } break;
        case 'o': if (Ends("ization")) { Replace("ize"); break; }
                  if (Ends("ation")) { Replace("ate"); break; }
                  if (Ends("ator")) { Replace("ate"); break; } break;
        case 's': if (Ends("alism")) { Replace("al"); break; }
                  if (Ends("iveness")) { Replace("ive"); break; }
                  if (Ends("fulness")) { Replace("ful"); break; }
                  if (Ends("ousness")) { Replace("ous"); break; } break;
        case 't': if (Ends("aliti")) { Replace("al"); break; }
                  if (Ends("iviti")) { Replace("ive"); break; }
                  if (Ends("biliti")) { Replace("ble"); break; } break;
        case 'g': if (Ends("logi")) { Replace("log"); break; }
    }
}

void PorterStemmer::Step3() {
    switch ((*word_)[k_]) {
        case 'e': if (Ends("icate")) { Replace("ic"); break; }
                  if (Ends("ative")) { Replace(""); break; }
                  if (Ends("alize")) { Replace("al"); break; } break;
        case 'i': if (Ends("iciti")) { Replace("ic"); break; } break;
        case 'l': if (Ends("ical")) { Replace("ic"); break; }
                  if (Ends("ful")) { Replace(""); break; } break;
        case 's': if (Ends("ness")) { Replace(""); break; } break;
    }
}

void PorterStemmer::Step4() {
    switch ((*word_)[k_ - 1]) {
        case 'a': if (Ends("al")) { if (M() > 1) k_ = j_; } break;
        case 'c': if (Ends("ance")) { if (M() > 1) k_ = j_; }
                  if (Ends("ence")) { if (M() > 1) k_ = j_; } break;
        case 'e': if (Ends("er")) { if (M() > 1) k_ = j_; } break;
        case 'i': if (Ends("ic")) { if (M() > 1) k_ = j_; } break;
        case 'l': if (Ends("able")) { if (M() > 1) k_ = j_; }
                  if (Ends("ible")) { if (M() > 1) k_ = j_; } break;
        case 'n': if (Ends("ant")) { if (M() > 1) k_ = j_; }
                  if (Ends("ement")) { if (M() > 1) k_ = j_; }
                  if (Ends("ment")) { if (M() > 1) k_ = j_; }
                  if (Ends("ent")) { if (M() > 1) k_ = j_; } break;
        case 'o': if (Ends("ion") && j_ >= 0) {
                      if ((*word_)[j_] == 's' || (*word_)[j_] == 't') {
                          if (M() > 1) k_ = j_;
                      }
                  }
                  if (Ends("ou")) { if (M() > 1) k_ = j_; } break;
        case 's': if (Ends("ism")) { if (M() > 1) k_ = j_; } break;
        case 't': if (Ends("ate")) { if (M() > 1) k_ = j_; }
                  if (Ends("iti")) { if (M() > 1) k_ = j_; } break;
        case 'u': if (Ends("ous")) { if (M() > 1) k_ = j_; } break;
        case 'v': if (Ends("ive")) { if (M() > 1) k_ = j_; } break;
        case 'z': if (Ends("ize")) { if (M() > 1) k_ = j_; } break;
    }
}

void PorterStemmer::Step5() {
    j_ = k_;
    if ((*word_)[k_] == 'e') {
        int a = M();
        if (a > 1 || (a == 1 && !Cvc(k_ - 1))) {
            word_->erase(k_);
            k_--;
        }
    }
    if ((*word_)[k_] == 'l' && DoubleCons(k_) && M() > 1) {
        word_->erase(k_);
        k_--;
    }
}

void PorterStemmer::Stem(std::string& word) {
    word_ = &word;
    k_ = static_cast<int>(word.size()) - 1;
    if (k_ <= 1) return;
    Step1();
    Step2();
    Step3();
    Step4();
    Step5();
    word_->resize(k_ + 1);
}

}  // namespace analysis
}  // namespace minilucene
