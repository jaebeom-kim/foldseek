#include "DBReader.h"
#include "IndexReader.h"
#include "DBWriter.h"
#include "Debug.h"
#include "Util.h"
#include "LocalParameters.h"
#include "MemoryMapped.h"
#include "createcomplexreport.h"

#ifdef OPENMP
#include <omp.h>
#endif

typedef std::pair<unsigned int, unsigned int> ChainKeyPair_t; // queryChain, dbChain

bool compareChainKeyPair_t(const ChainKeyPair_t &first, const ChainKeyPair_t &second) {
    if (first.first < second.first)
        return true;
    if (first.first > second.first)
        return false;
    if (first.second < second.second)
        return true;
    if (first.second > second.second)
        return false;
    return false;
}

int expandcomplex(int argc, const char **argv, const Command &command) {
    LocalParameters &par = LocalParameters::getLocalInstance();
    par.parseParameters(argc, argv, command, true, 0, MMseqsParameter::COMMAND_ALIGN);
    std::string qLookupFile = par.db1 + ".lookup";
    std::string dbLookupFile = par.db2 + ".lookup";
    DBReader<unsigned int> alnDbr(par.db3.c_str(), par.db3Index.c_str(), par.threads, DBReader<unsigned int>::USE_INDEX|DBReader<unsigned int>::USE_DATA);
    alnDbr.open(DBReader<unsigned int>::LINEAR_ACCCESS);
    DBWriter resultWriter(par.db4.c_str(), par.db4Index.c_str(), static_cast<unsigned int>(par.threads), par.compressed, Parameters::DBTYPE_PREFILTER_RES);
    resultWriter.open();
    std::vector<unsigned int> qComplexIndices;
    std::vector<unsigned int> dbComplexIndices;
    chainKeyToComplexId_t qChainKeyToComplexIdMap;
    chainKeyToComplexId_t dbChainKeyToComplexIdMap;
    complexIdToChainKeys_t qComplexIdToChainKeysMap;
    complexIdToChainKeys_t dbComplexIdToChainKeysMap;
    getKeyToIdMapIdToKeysMapIdVec(qLookupFile, qChainKeyToComplexIdMap, qComplexIdToChainKeysMap, qComplexIndices);
    getKeyToIdMapIdToKeysMapIdVec(dbLookupFile, dbChainKeyToComplexIdMap, dbComplexIdToChainKeysMap, dbComplexIndices);
    dbComplexIndices.clear();
    qChainKeyToComplexIdMap.clear();
    Debug::Progress progress(qComplexIndices.size());

#pragma omp parallel
    {
        unsigned int thread_idx = 0;
#ifdef OPENMP
        thread_idx = static_cast<unsigned int>(omp_get_thread_num());
#endif
        resultToWrite_t result;
        std::vector<unsigned int> dbFoundIndices;
        std::vector<ChainKeyPair_t>  chainKeyPairs;
#pragma omp for schedule(dynamic, 1)
        // for each q complex
        for (size_t qCompIdx = 0; qCompIdx < qComplexIndices.size(); qCompIdx++) {
            unsigned int qComplexId = qComplexIndices[qCompIdx];
            std::vector<unsigned int> &qChainKeys = qComplexIdToChainKeysMap.at(qComplexId);
            // For the current query complex
            for (size_t qChainIdx=0; qChainIdx<qChainKeys.size(); qChainIdx++) {
                unsigned int qKey = alnDbr.getId(qChainKeys[qChainIdx]);
                if (qKey == NOT_AVAILABLE_CHAIN_KEY)
                    continue;
                char *data = alnDbr.getData(qKey, thread_idx);
                while (*data != '\0') {
                    char dbKeyBuffer[255 + 1];
                    Util::parseKey(data, dbKeyBuffer);
                    const auto dbChainKey = (unsigned int) strtoul(dbKeyBuffer, NULL, 10);
                    const unsigned int dbComplexId = dbChainKeyToComplexIdMap.at(dbChainKey);
                    // find all db complex aligned to the query complex.
                    if (std::find(dbFoundIndices.begin(), dbFoundIndices.end(), dbComplexId) == dbFoundIndices.end())
                        dbFoundIndices.emplace_back(dbComplexId);
                    data = Util::skipLine(data);
                }
            }
            if (dbFoundIndices.empty())
                continue;
            // Among all db complexes aligned to query complex
            for (size_t dbIdx=0; dbIdx<dbFoundIndices.size(); dbIdx++) {
                std::vector<unsigned int> &dbChainKeys = dbComplexIdToChainKeysMap.at(dbFoundIndices[dbIdx]);
                // for all query chains
                for (size_t qChainIdx=0; qChainIdx<qChainKeys.size(); qChainIdx++) {
                    // and target chains
                    for (size_t dbChainIdx=0; dbChainIdx<dbChainKeys.size(); dbChainIdx++) {
                        // get all possible alignments
                        chainKeyPairs.emplace_back(qChainKeys[qChainIdx], dbChainKeys[dbChainIdx]);
                    }
                }
            }
            SORT_SERIAL(chainKeyPairs.begin(), chainKeyPairs.end(), compareChainKeyPair_t);
            unsigned int qPrevChainKey = chainKeyPairs[0].first;
            // and write.
            for (size_t chainKeyPairIdx=0; chainKeyPairIdx<chainKeyPairs.size(); chainKeyPairIdx++) {
                if (chainKeyPairs[chainKeyPairIdx].first != qPrevChainKey) {
                    resultWriter.writeData(result.c_str(),result.length(),qPrevChainKey,thread_idx);
                    result.clear();
                    qPrevChainKey = chainKeyPairs[chainKeyPairIdx].first;
                }
                result.append(SSTR(chainKeyPairs[chainKeyPairIdx].second));
                result.push_back('\n');
            }
            resultWriter.writeData(result.c_str(),result.length(),qPrevChainKey,thread_idx);
            result.clear();
            dbFoundIndices.clear();
            chainKeyPairs.clear();
            progress.updateProgress();
        }
    }
    qComplexIndices.clear();
    dbChainKeyToComplexIdMap.clear();
    qComplexIdToChainKeysMap.clear();
    dbComplexIdToChainKeysMap.clear();
    alnDbr.close();
    resultWriter.close(true);
    return EXIT_SUCCESS;
}