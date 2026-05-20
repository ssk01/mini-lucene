#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include <gtest/gtest.h>

namespace minilucene {
namespace document {

TEST(Document, FactoryMethodFlags) {
    auto kw = Field::Keyword("url", "http://example.com");
    EXPECT_TRUE(kw.IsStored());
    EXPECT_TRUE(kw.IsIndexed());
    EXPECT_FALSE(kw.IsTokenized());

    auto txt = Field::Text("title", "Hello World");
    EXPECT_TRUE(txt.IsStored());
    EXPECT_TRUE(txt.IsIndexed());
    EXPECT_TRUE(txt.IsTokenized());

    auto unidx = Field::UnIndexed("meta", "some metadata");
    EXPECT_TRUE(unidx.IsStored());
    EXPECT_FALSE(unidx.IsIndexed());
    EXPECT_FALSE(unidx.IsTokenized());

    auto unstored = Field::UnStored("body", "long text here");
    EXPECT_FALSE(unstored.IsStored());
    EXPECT_TRUE(unstored.IsIndexed());
    EXPECT_TRUE(unstored.IsTokenized());
}

TEST(Document, AddAndGetField) {
    Document doc;
    doc.Add(Field::Keyword("id", "123"));
    doc.Add(Field::Text("title", "Hello"));

    const Field* f = doc.GetField("title");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->Value(), "Hello");

    EXPECT_EQ(doc.GetField("nonexistent"), nullptr);
}

TEST(Document, FieldsIteration) {
    Document doc;
    doc.Add(Field::Keyword("a", "1"));
    doc.Add(Field::Text("b", "2"));
    doc.Add(Field::UnIndexed("c", "3"));

    EXPECT_EQ(doc.Fields().size(), 3);
}

}  // namespace document
}  // namespace minilucene
