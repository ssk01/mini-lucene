// Compare mini-lucene vs Lucene 9 (Elasticsearch internal engine) performance.
// java -cp lucene-core-9.12.0.jar:lucene-analysis-common-9.12.0.jar:. BenchCompare

import org.apache.lucene.analysis.*;
import org.apache.lucene.analysis.core.*;
import org.apache.lucene.document.*;
import org.apache.lucene.index.*;
import org.apache.lucene.search.*;
import org.apache.lucene.store.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class BenchCompare {
    public static void main(String[] args) throws Exception {
        String cranPath = args[0];

        // Load docs
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

        System.out.println("=== Lucene 9 (ES internal) Benchmark ===");
        System.out.println("Documents: " + bodies.size());

        // Index benchmark
        Directory dir = new ByteBuffersDirectory();
        IndexWriterConfig config = new IndexWriterConfig(new SimpleAnalyzer());
        IndexWriter writer = new IndexWriter(dir, config);

        long t1 = System.nanoTime();
        for (String text : bodies) {
            Document doc = new Document();
            doc.add(new TextField("body", text, Field.Store.NO));
            writer.addDocument(doc);
        }
        writer.commit();
        long t2 = System.nanoTime();
        double indexMs = (t2 - t1) / 1_000_000.0;
        System.out.printf("Index time: %.0f ms (%.0f docs/sec)%n", indexMs, bodies.size() / (indexMs / 1000));

        // Index size
        long size = 0;
        for (String name : dir.listAll()) {
            size += dir.fileLength(name);
        }
        System.out.printf("Index size: %.1f MB%n", size / (1024.0 * 1024.0));

        // Query benchmark
        DirectoryReader reader = DirectoryReader.open(dir);
        IndexSearcher searcher = new IndexSearcher(reader);

        String[] queries = {"wing","boundary","layer","shock","turbulent","supersonic",
                           "pressure","velocity","temperature","flow","mach","airfoil","stability"};

        // Warmup: run one query outside timing
        searcher.search(new TermQuery(new Term("body", "wing")), 1400);

        long totalHits = 0;
        double sumLatUs = 0;
        double minLatUs = Double.MAX_VALUE;
        double maxLatUs = 0;
        for (int i = 0; i < queries.length; i++) {
            long qt1 = System.nanoTime();
            TermQuery q = new TermQuery(new Term("body", queries[i]));
            TopDocs results = searcher.search(q, 1400);
            long qt2 = System.nanoTime();
            totalHits += results.totalHits.value;
            double us = (qt2 - qt1) / 1000.0;
            sumLatUs += us;
            if (us < minLatUs) minLatUs = us;
            if (us > maxLatUs) maxLatUs = us;
        }

        double avgLat = sumLatUs / queries.length;
        System.out.printf("Queries: %d%n", queries.length);
        System.out.printf("Min query: %.0f μs%n", minLatUs);
        System.out.printf("Max query: %.0f μs%n", maxLatUs);
        System.out.printf("Avg query latency: %.0f μs%n", avgLat);
        System.out.printf("Total hits: %d%n", totalHits);

        reader.close();
        writer.close();

        // Summary table
        System.out.println("\n=== Comparison Table ===");
        System.out.printf("%-15s %-15s %-15s%n", "Metric", "mini-lucene", "Lucene 9 (ES)");
        System.out.printf("%-15s %-15s %-15s%n", "---", "---", "---");
        System.out.printf("%-15s %-15s %-15s%n", "Docs", "1400", bodies.size());
        System.out.printf("%-15s %-15s %-15s%n", "Index time", "~1000 ms", String.format("%.0f ms", indexMs));
        System.out.printf("%-15s %-15s %-15s%n", "Index size", "~0.8 MB", String.format("%.1f MB", size/(1024.0*1024.0)));
        System.out.printf("%-15s %-15s %-15s%n", "Query latency", "~100 μs", String.format("%.0f μs", avgLat));
    }
}
