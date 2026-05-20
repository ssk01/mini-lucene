import org.apache.lucene.analysis.*;
import org.apache.lucene.analysis.core.*;
import org.apache.lucene.document.*;
import org.apache.lucene.index.*;
import org.apache.lucene.search.*;
import org.apache.lucene.store.*;
import java.io.*;

public class GroundTruthGenerator {
    public static void main(String[] args) throws Exception {
        Directory dir = new ByteBuffersDirectory();
        IndexWriterConfig config = new IndexWriterConfig(new SimpleAnalyzer());
        IndexWriter writer = new IndexWriter(dir, config);

        String[] docs = {
            "The quick brown fox jumps over the lazy dog",
            "Apache Lucene is a free text search engine library",
            "Hello World from mini-lucene C++ full-text search engine",
            "The fox and the hound were running in the forest",
            "Running quickly is good for your health",
            "The quickest way to jump over the lazy fox",
            "Generalization of the stemming algorithm is complex",
            "Consignment of goods arrived at the connection terminal",
            "Connected devices are hopping on the network",
            "Troubles with the troubleshooting module"
        };

        for (String text : docs) {
            Document doc = new Document();
            doc.add(new TextField("body", text, Field.Store.YES));
            writer.addDocument(doc);
        }
        writer.commit();

        DirectoryReader reader = DirectoryReader.open(dir);
        IndexSearcher searcher = new IndexSearcher(reader);

        String[][] queries = {
            {"fox", "term frequency varies"},
            {"the", "common term"},
            {"lucene", "rare term"},
        };

        for (String[] q : queries) {
            String queryText = q[0];
            Query query = new TermQuery(new Term("body", queryText));
            TopDocs results = searcher.search(query, 10);

            System.out.println("// Query: \"" + queryText + "\"");
            System.out.println("// Total hits: " + results.totalHits.value);
            for (int i = 0; i < Math.min(results.scoreDocs.length, 5); i++) {
                ScoreDoc sd = results.scoreDocs[i];
                System.out.printf("//   doc%d : %.6f%n", sd.doc, sd.score);
            }
            System.out.println();
        }

        reader.close();
        writer.close();
    }
}
