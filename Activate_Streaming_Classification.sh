#!/bin/bash

#PATH = "/Volumes/transfer/MinION_Data_Transfer/"

cd /Volumes/transfer/MinION_Data_Transfer/
rm mylittle.fasta
curr=$(date +%Y%m%d%H%M)

while true
do

        echo "CHECK FILE"
        if [  "$(ls -A Current_Run/)" ]
        then

            echo "Step 1: fastq to fasta."
            find Current_Run/ -name "*.fast5" | cat > tmp.txt
            poretools fasta ./Current_Run > mylittle.fasta
            xargs rm -r <tmp.txt
            rm tmp.txt

            echo "Step 2: GO KRAKEN"
            cd /Volumes/transfer/kraken/downloads/
            start=$SECONDS
            ./kraken /Volumes/transfer/MinION_Data_Transfer/mylittle.fasta --db1 kraken_db_112916_hum/ --db2 kraken_db_bac/ --threads 8 --fasta-input --only-classified-output --output out.txt
            cat out.txt >> /Volumes/transfer/MinION_Data_Transfer/FinalOut_$curr.txt
            rm out.txt

            echo "Step 3: GO TRANSLATE -- Notification"
            ./kraken-report --db kraken_db_bac /Volumes/transfer/MinION_Data_Transfer/FinalOut_$curr.txt > /Volumes/transfer/MinION_Data_Transfer/FinalReport_$curr.txt
            cd /Volumes/transfer/MinION_Data_Transfer/
            ./outoa FinalOut_$curr.txt FinalFasta_$curr.fasta

            DURATION=$(( SECONDS-start))
            echo "Time used kraken: \"$DURATION\""


            echo "Step 4: remove mylittle.fasta"
            rm mylittle.fasta


            echo "TADA~~ You are done for this round!"


        fi
        sleep 120

done

