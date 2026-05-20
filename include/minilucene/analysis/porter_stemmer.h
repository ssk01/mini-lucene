#pragma once

#include <string>

namespace minilucene {
namespace analysis {

class PorterStemmer {
public:
    void Stem(std::string& word);

private:
    int k_, j_;
    std::string* word_;

    bool Cons(int i) const;
    int M() const;
    bool VowelInStem() const;
    bool DoubleCons(int i) const;
    bool LiEnding(int i) const;
    bool Cvc(int i) const;
    bool Ends(const std::string& s);
    void SetTo(const std::string& s);
    void Replace(const std::string& s);

    void Step1();
    void Step2();
    void Step3();
    void Step4();
    void Step5();
};

}  // namespace analysis
}  // namespace minilucene
