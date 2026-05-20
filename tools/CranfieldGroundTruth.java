// Index Cranfield with Lucene 9, output exact doc IDs per query term.
// java -cp lucene-core-9.12.0.jar:lucene-analysis-common-9.12.0.jar:. CranfieldGroundTruth

import org.apache.lucene.analysis.*;
import org.apache.lucene.analysis.core.*;
import org.apache.lucene.document.*;
import org.apache.lucene.index.*;
import org.apache.lucene.search.*;
import org.apache.lucene.store.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class CranfieldGroundTruth {
    public static void main(String[] args) throws Exception {
        String cranPath = args[0];
        String outputPath = args.length > 1 ? args[1] : "/tmp/cran_ground_truth.txt";

        // Read Cranfield docs
        List<String> bodies = new ArrayList<>();
        StringBuilder cur = new StringBuilder();
        boolean inContent = false;
        for (String line : Files.readAllLines(Paths.get(cranPath))) {
            if (line.startsWith(".I")) {
                if (cur.length() > 0) { bodies.add(cur.toString().trim()); cur = new StringBuilder(); }
                inContent = false;
            } else if (line.equals(".W")) { inContent = true; }
            else if (line.equals(".T")) { inContent = true; }
            else if (line.equals(".A") || line.equals(".B")) { inContent = false; }
            else if (inContent && !line.isEmpty()) {
                if (cur.length() > 0) cur.append(" ");
                cur.append(line);
            }
        }
        if (cur.length() > 0) bodies.add(cur.toString().trim());
        System.err.println("Loaded " + bodies.size() + " docs");

        // Index with Lucene SimpleAnalyzer
        Directory dir = new ByteBuffersDirectory();
        IndexWriterConfig config = new IndexWriterConfig(new SimpleAnalyzer());
        IndexWriter writer = new IndexWriter(dir, config);
        for (String text : bodies) {
            Document doc = new Document();
            doc.add(new TextField("body", text, Field.Store.NO));
            writer.addDocument(doc);
        }
        writer.commit();

        DirectoryReader reader = DirectoryReader.open(dir);
        IndexSearcher searcher = new IndexSearcher(reader);

        // Count unique terms
        Terms terms = MultiTerms.getTerms(reader, "body");
        System.err.println("Total terms in index: " + (terms != null ? terms.size() : "N/A"));

        // Query specific terms and output exact doc IDs
        String[] testTerms = {"boundary", "layer", "wing", "shock", "turbulent",
                              "supersonic", "pressure", "velocity", "temperature", "flow",
                              "mach", "airfoil", "boundary", "layer", "stability"};

        try (PrintWriter pw = new PrintWriter(outputPath)) {
            for (String termText : testTerms) {
                TermQuery query = new TermQuery(new Term("body", termText));
                TopDocs results = searcher.search(query, 1400);
                StringBuilder sb = new StringBuilder();
                sb.append(termText).append("|").append(results.totalHits.value).append("|");
                for (int i = 0; i < results.scoreDocs.length; i++) {
                    if (i > 0) sb.append(",");
                    sb.append(results.scoreDocs[i].doc);
                }
                pw.println(sb);
                System.err.println("  " + termText + ": " + results.totalHits.value + " hits");
            }
        }

        reader.close();
        writer.close();
        System.err.println("Ground truth written to " + outputPath);
    }
}
