Kraken taxonomic sequence classification system
===============================================

Please see the [Kraken webpage] or the [Kraken manual]
for information on installing and operating Kraken.
A local copy of the [Kraken manual] is also present here
in the `docs/` directory (`MANUAL.html` and `MANUAL.markdown`).

[Kraken webpage]:   http://ccb.jhu.edu/software/kraken/
[Kraken manual]:    http://ccb.jhu.edu/software/kraken/MANUAL.html



ANBYEW'S EDIT:
What's customized?
1. Due to the hard ware limitation, we have to separate the human database vs the rest (including bacteria, fungi, protozoa, and virus) database. This version of Kraken supports two database, and will run through the human database first. For all the sequences that are unclassified as human, it will run through the second database, and determine the classification. Also, all the raw sequences were listed in the output of kraken, in case we need this information later.
2. Activate_Streaming_Classification.sh is a pipeline that grabs newly generated sequences in the "MinION_DATA_Transfer" folder every 2 mins, run Kraken and Kraken-report, and generates result report in real time. The first version supports fast5 format, while the second version supports fastq format.
3. outoa is a c++ program that translates the output into fasta format, in case we want to double check the classified sequences. 
4. UPDATE APRIL 17, 2018: It was found that several human genome were classified as E.coli. It turned out that these sequences have lots of root hit, but only a few(usually fewer than 10) k-mer hits on actual E.Coli. This is due to the lab treatment, and should still be unclassified as E.Coli. Therefore, we discarded all the root hits (taxid = 1), and added on "--thhold NUM" option to filter out all the sequences with hits less than NUM. This option differs from the "--min-hits NUM" option in that the quick mode doesn't have to be turned on. The intention behind this change is to increase the confidence on the results, and filter out the less confident reads.  

