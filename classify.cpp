/*
 * Copyright 2013-2015, Derrick Wood <dwood@cs.jhu.edu>
 *
 * This file is part of the Kraken taxonomic sequence classification system.
 *
 * Kraken is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Kraken is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Kraken.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "kraken_headers.hpp"
#include "krakendb.hpp"
#include "krakenutil.hpp"
#include "quickfile.hpp"
#include "seqreader.hpp"

const size_t DEF_WORK_UNIT_SIZE = 500000;

using namespace std;
using namespace kraken;

void parse_command_line(int argc, char **argv);
void usage(int exit_code=EX_USAGE);
void process_file(char *filename);
void classify_sequence1(DNASequence &dna, ostringstream &koss,
                       ostringstream &coss, ostringstream &uoss);
void classify_sequence2(DNASequence &dna, ostringstream &koss,
                        ostringstream &coss, ostringstream &uoss);
string hitlist_string(vector<uint32_t> &taxa, vector<uint8_t> &ambig);
set<uint32_t> get_ancestry(uint32_t taxon);
void report_stats(struct timeval time1, struct timeval time2);

int Num_threads = 1;
string DB_filename1, DB_filename2, Index_filename1, Index_filename2, Nodes_filename;
bool Quick_mode = false;
bool Fastq_input = false;
bool Print_classified = false;
bool Print_unclassified = false;
bool Print_kraken = true;
bool Populate_memory = false;
bool Only_classified_kraken_output = false;
uint32_t Minimum_hit_count = 1;
map<uint32_t, uint32_t> Parent_map;
KrakenDB Database1;
KrakenDB Database2;
string Classified_output_file, Unclassified_output_file, Kraken_output_file;
ostream *Classified_output;
ostream *Unclassified_output;
ostream *Kraken_output;
size_t Work_unit_size = DEF_WORK_UNIT_SIZE;

uint64_t total_classified = 0;
uint64_t total_classified2 = 0;
uint64_t total_sequences = 0;
uint64_t total_bases = 0;

int main(int argc, char **argv) {
    
    cout << "Hello World!" << endl;
  #ifdef _OPENMP
  omp_set_num_threads(1);
  #endif

  parse_command_line(argc, argv);
  if (! Nodes_filename.empty())
    Parent_map = build_parent_map(Nodes_filename);

  if (Populate_memory)
    cerr << "Loading database... ";

  QuickFile db_file1;
  db_file1.open_file(DB_filename1);
  QuickFile db_file2;
  db_file2.open_file(DB_filename2);
  if (Populate_memory)
    db_file1.load_file();
  if (Populate_memory)
    db_file2.load_file();
  Database1 = KrakenDB(db_file1.ptr());
  Database2 = KrakenDB(db_file2.ptr());
  KmerScanner::set_k(Database1.get_k());
  KmerScanner::set_k(Database2.get_k());

  QuickFile idx_file1;
  idx_file1.open_file(Index_filename1);
  QuickFile idx_file2;
  idx_file2.open_file(Index_filename2);
  if (Populate_memory)
    idx_file1.load_file();
  if (Populate_memory)
    idx_file2.load_file();
  KrakenDBIndex db_index1(idx_file1.ptr());
  KrakenDBIndex db_index2(idx_file2.ptr());
  Database1.set_index(&db_index1);
  Database2.set_index(&db_index2);

  if (Populate_memory)
    cerr << "complete." << endl;

  if (Print_classified) {
    if (Classified_output_file == "-")
      Classified_output = &cout;
    else
      Classified_output = new ofstream(Classified_output_file.c_str());
  }

  if (Print_unclassified) {
    if (Unclassified_output_file == "-")
      Unclassified_output = &cout;
    else
      Unclassified_output = new ofstream(Unclassified_output_file.c_str());
  }

  if (! Kraken_output_file.empty()) {
    if (Kraken_output_file == "-")
      Print_kraken = false;
    else
      Kraken_output = new ofstream(Kraken_output_file.c_str());
  }
  else
    Kraken_output = &cout;

  struct timeval tv1, tv2;
  gettimeofday(&tv1, NULL);
  for (int i = optind; i < argc; i++)
    process_file(argv[i]);
  gettimeofday(&tv2, NULL);

  report_stats(tv1, tv2);

  return 0;
}

void report_stats(struct timeval time1, struct timeval time2) {
  time2.tv_usec -= time1.tv_usec;
  time2.tv_sec -= time1.tv_sec;
  if (time2.tv_usec < 0) {
    time2.tv_sec--;
    time2.tv_usec += 1000000;
  }
  double seconds = time2.tv_usec;
  seconds /= 1e6;
  seconds += time2.tv_sec;

  cerr << "\r";
  fprintf(stderr, 
          "%llu sequences (%.2f Mbp) processed in %.3fs (%.1f Kseq/m, %.2f Mbp/m).\n",
          (unsigned long long) total_sequences, total_bases / 1.0e6, seconds,
          total_sequences / 1.0e3 / (seconds / 60),
          total_bases / 1.0e6 / (seconds / 60) );
  fprintf(stderr, "  %llu sequences classified as human(%.2f%%)\n",
          (unsigned long long) total_classified, total_classified * 100.0 / total_sequences);
  fprintf(stderr, "  %llu sequences unclassified as human(%.2f%%)\n",
          (unsigned long long) (total_sequences - total_classified),
          (total_sequences - total_classified) * 100.0 / total_sequences);
    fprintf(stderr, "  %llu sequences in DB2 classified (%.2f%%)\n",
            (unsigned long long) total_classified2, total_classified2 * 100.0 / (total_sequences - total_classified));
    fprintf(stderr, "  %llu sequences in DB2 unclassified (%.2f%%)\n",
            (unsigned long long) (total_sequences - total_classified - total_classified2),
            (total_sequences - total_classified - total_classified2) * 100.0 / (total_sequences - total_classified));
}

void process_file(char *filename) {
  string file_str(filename);
  DNASequenceReader *reader;
  DNASequence dna;

  if (Fastq_input)
    reader = new FastqReader(file_str);
  else
    reader = new FastaReader(file_str);

  #pragma omp parallel
  {
    vector<DNASequence> work_unit;
    ostringstream kraken_output_ss, classified_output_ss, unclassified_output_ss;

    while (reader->is_valid()) {
      work_unit.clear();
      size_t total_nt = 0;
      #pragma omp critical(get_input)
      {
        while (total_nt < Work_unit_size) {
          dna = reader->next_sequence();
          if (! reader->is_valid())
            break;
          work_unit.push_back(dna);
          total_nt += dna.seq.size();
        }
      }
      if (total_nt == 0)
        break;
      
      kraken_output_ss.str("");
      classified_output_ss.str("");
      unclassified_output_ss.str("");
      for (size_t j = 0; j < work_unit.size(); j++)
        classify_sequence1( work_unit[j], kraken_output_ss,
                           classified_output_ss, unclassified_output_ss );

      #pragma omp critical(write_output)
      {
        if (Print_kraken)
          (*Kraken_output) << kraken_output_ss.str();
        if (Print_classified)
          (*Classified_output) << classified_output_ss.str();
        if (Print_unclassified)
          (*Unclassified_output) << unclassified_output_ss.str();
        total_sequences += work_unit.size();
        total_bases += total_nt;
        cerr << "\rProcessed " << total_sequences << " sequences (" << total_bases << " bp) ...";
      }
    }
  }  // end parallel section

  delete reader;
}

void classify_sequence1(DNASequence &dna, ostringstream &koss,
                       ostringstream &coss, ostringstream &uoss) {
  vector<uint32_t> taxa;
  vector<uint8_t> ambig_list;
  map<uint32_t, uint32_t> hit_counts;
  uint64_t *kmer_ptr;
  uint32_t taxon = 0;
  uint32_t hits = 0;  // only maintained if in quick mode

  uint64_t current_bin_key;
  int64_t current_min_pos = 1;
  int64_t current_max_pos = 0;

  if (dna.seq.size() >= Database1.get_k()) {
    KmerScanner scanner(dna.seq);
    while ((kmer_ptr = scanner.next_kmer()) != NULL) {
      taxon = 0;
      if (scanner.ambig_kmer()) {
        ambig_list.push_back(1);
      }
      else {
        ambig_list.push_back(0);
        uint32_t *val_ptr = Database1.kmer_query(
                              Database1.canonical_representation(*kmer_ptr),
                              &current_bin_key,
                              &current_min_pos, &current_max_pos
                            );
        taxon = val_ptr ? *val_ptr : 0;
        if (taxon) {
          hit_counts[taxon]++;
          if (Quick_mode && ++hits >= Minimum_hit_count)
            break;
        }
      }
      taxa.push_back(taxon);
    }
  }

  uint32_t call = 0;
  if (Quick_mode)
    call = hits >= Minimum_hit_count ? taxon : 0;
  else
    call = resolve_tree(hit_counts, Parent_map);

  if (call)
    #pragma omp atomic
    total_classified++;
    
    
    ////////// if (!call)///////////
    ////////他的sequence是一条一条跑的，不存储，所以可以直接process!///
    //// 下面的都不用打印出来了//////
    if (!call) {
        classify_sequence2(dna, koss, coss, uoss);
    }
    
/*
  if (Print_unclassified || Print_classified) {
    ostringstream *oss_ptr = call ? &coss : &uoss;
    bool print = call ? Print_classified : Print_unclassified;
    if (print) {
      if (Fastq_input) {
        (*oss_ptr) << "@" << dna.header_line << endl
            << dna.seq << endl
            << "+" << endl
            << dna.quals << endl;
      }
      else {
        (*oss_ptr) << ">" << dna.header_line << endl
            << dna.seq << endl;
      }
    }
  }

  if (! Print_kraken)
    return;

  if (call) {
    koss << "C\t";
  }
  else {
    if (Only_classified_kraken_output)
      return;
    koss << "U\t";
  }
  koss << dna.id << "\t" << call << "\t" << dna.seq.size() << "\t";

  if (Quick_mode) {
    koss << "Q:" << hits;
  }
  else {
    if (taxa.empty())
      koss << "0:0";
    else
      koss << hitlist_string(taxa, ambig_list);
  }

  koss << endl;

 
 */
}








void classify_sequence2(DNASequence &dna, ostringstream &koss,
                        ostringstream &coss, ostringstream &uoss) {
    vector<uint32_t> taxa;
    vector<uint8_t> ambig_list;
    map<uint32_t, uint32_t> hit_counts;
    uint64_t *kmer_ptr;
    uint32_t taxon = 0;
    uint32_t hits = 0;  // only maintained if in quick mode
    
    uint64_t current_bin_key;
    int64_t current_min_pos = 1;
    int64_t current_max_pos = 0;
    
    if (dna.seq.size() >= Database2.get_k()) {
        KmerScanner scanner(dna.seq);
        while ((kmer_ptr = scanner.next_kmer()) != NULL) {
            taxon = 0;
            if (scanner.ambig_kmer()) {
                ambig_list.push_back(1);
            }
            else {
                ambig_list.push_back(0);
                uint32_t *val_ptr = Database2.kmer_query(
                                                         Database2.canonical_representation(*kmer_ptr),
                                                         &current_bin_key,
                                                         &current_min_pos, &current_max_pos
                                                         );
                taxon = val_ptr ? *val_ptr : 0;
                if (taxon) {
                    hit_counts[taxon]++;
                    if (Quick_mode && ++hits >= Minimum_hit_count)
                        break;
                }
            }
            taxa.push_back(taxon);
        }
    }
    
    uint32_t call = 0;
    if (Quick_mode)
        call = hits >= Minimum_hit_count ? taxon : 0;
    else
        call = resolve_tree(hit_counts, Parent_map);
    
    if (call)
        #pragma omp atomic
       
        total_classified2++;
    
    
    if (Print_unclassified || Print_classified) {
        ostringstream *oss_ptr = call ? &coss : &uoss;
        bool print = call ? Print_classified : Print_unclassified;
        if (print) {
            if (Fastq_input) {
                (*oss_ptr) << "@" << dna.header_line << endl
                << dna.seq << endl
                << "+" << endl
                << dna.quals << endl;
            }
            else {
                (*oss_ptr) << ">" << dna.header_line << endl
                << dna.seq << endl;
            }
        }
    }
    
    if (! Print_kraken)
        return;
    
    if (call) {
        koss << "C\t";
    }
    else {
        if (Only_classified_kraken_output)
            return;
        koss << "U\t";
    }
    koss << dna.id << "\t" << call << "\t" << dna.seq.size() << "\t";
    
    if (Quick_mode) {
        koss << "Q:" << hits;
    }
    else {
        if (taxa.empty())
            koss << "0:0";
        else
            koss << hitlist_string(taxa, ambig_list);
    }
    
    koss << endl;
}



















string hitlist_string(vector<uint32_t> &taxa, vector<uint8_t> &ambig)
{
  int64_t last_code;
  int code_count = 1;
  ostringstream hitlist;

  if (ambig[0])   { last_code = -1; }
  else            { last_code = taxa[0]; }

  for (size_t i = 1; i < taxa.size(); i++) {
    int64_t code;
    if (ambig[i]) { code = -1; }
    else          { code = taxa[i]; }

    if (code == last_code) {
      code_count++;
    }
    else {
      if (last_code >= 0) {
        hitlist << last_code << ":" << code_count << " ";
      }
      else {
        hitlist << "A:" << code_count << " ";
      }
      code_count = 1;
      last_code = code;
    }
  }
  if (last_code >= 0) {
    hitlist << last_code << ":" << code_count;
  }
  else {
    hitlist << "A:" << code_count;
  }
  return hitlist.str();
}

set<uint32_t> get_ancestry(uint32_t taxon) {
  set<uint32_t> path;

  while (taxon > 0) {
    path.insert(taxon);
    taxon = Parent_map[taxon];
  }
  return path;
}

void parse_command_line(int argc, char **argv) {
  int opt;
  int sig;

  if (argc > 1 && strcmp(argv[1], "-h") == 0)
    usage(0);
  while ((opt = getopt(argc, argv, "d:b:i:j:t:u:n:m:o:qfcC:U:M")) != -1) {
    switch (opt) {
      case 'd' :
        DB_filename1 = optarg;
        break;
      case 'b' :
        DB_filename2 = optarg;
        break;
      case 'i' :
        Index_filename1 = optarg;
        break;
      case 'j' :
        Index_filename2 = optarg;
        break;
      case 't' :
        sig = atoi(optarg);
        if (sig <= 0)
          errx(EX_USAGE, "can't use nonpositive thread count");
        #ifdef _OPENMP
        Num_threads = sig;
        omp_set_num_threads(Num_threads);
        #endif
        break;
      case 'n' :
        Nodes_filename = optarg;
        break;
      case 'q' :
        Quick_mode = true;
        break;
      case 'm' :
        sig = atoi(optarg);
        if (sig <= 0)
          errx(EX_USAGE, "can't use nonpositive minimum hit count");
        Minimum_hit_count = sig;
        break;
      case 'f' :
        Fastq_input = true;
        break;
      case 'c' :
        Only_classified_kraken_output = true;
        break;
      case 'C' :
        Print_classified = true;
        Classified_output_file = optarg;
        break;
      case 'U' :
        Print_unclassified = true;
        Unclassified_output_file = optarg;
        break;
      case 'o' :
        Kraken_output_file = optarg;
        break;
      case 'u' :
        sig = atoi(optarg);
        if (sig <= 0)
          errx(EX_USAGE, "can't use nonpositive work unit size");
        Work_unit_size = sig;
        break;
      case 'M' :
        Populate_memory = true;
        break;
      default:
        usage();
        break;
    }
  }

  if (DB_filename1.empty()) {
    cerr << "Missing mandatory option -d" << endl;
    usage();
  }
  if (DB_filename2.empty()) {
    cerr << "Missing mandatory option -b" << endl;
    usage();
  }
  if (Index_filename1.empty()) {
    cerr << "Missing mandatory option -i" << endl;
    usage();
  }
  if (Index_filename2.empty()) {
    cerr << "Missing mandatory option -j" << endl;
    usage();
  }
  if (Nodes_filename.empty() && ! Quick_mode) {
    cerr << "Must specify one of -q or -n" << endl;
    usage();
  }
  if (optind == argc) {
    cerr << "No sequence data files specified" << endl;
  }
}

void usage(int exit_code) {
  cerr << "Usage: classify [options] <fasta/fastq file(s)>" << endl
       << endl
       << "Options: (*mandatory)" << endl
       << "* -d filename      Kraken DB filename1" << endl
       << "* -b filename      Kraken DB filename2" << endl
       << "* -i filename      Kraken DB1 index filename" << endl
       << "* -j filename      Kraken DB2 index filename" << endl
       << "  -n filename      NCBI Taxonomy nodes file" << endl
       << "  -o filename      Output file for Kraken output" << endl
       << "  -t #             Number of threads" << endl
       << "  -u #             Thread work unit size (in bp)" << endl
       << "  -q               Quick operation" << endl
       << "  -m #             Minimum hit count (ignored w/o -q)" << endl
       << "  -C filename      Print classified sequences" << endl
       << "  -U filename      Print unclassified sequences" << endl
       << "  -f               Input is in FASTQ format" << endl
       << "  -c               Only include classified reads in output" << endl
       << "  -M               Preload database files" << endl
       << "  -h               Print this message" << endl
       << endl
       << "At least one FASTA or FASTQ file must be specified." << endl
       << "Kraken output is to standard output by default." << endl;
  exit(exit_code);
}
