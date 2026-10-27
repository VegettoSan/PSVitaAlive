#include "installer/zip_recovery_journal.hpp"
#include "installer/saved_zip_parts.hpp"
#include <cassert>
int main() {
    using namespace psvitaalive;
    const std::string job="ux0:data/psvitaalive/downloads/jobs/job_42";
    ZipRecoveryTransaction tx, parsed;
    tx.source=job+"/payload.zip";
    tx.base="ux0:data/psvitaalive/manual/PCSA12345/payload.zip";
    tx.info="PS Vita Alive Store - ZIP manual recovery\n\nApp ID: test\n"
            "Title ID: PCSA12345\nArchive parts: 4\n"
            "Manual extraction target: ux0:data/game\n";
    for (unsigned p : {1u,2u,4u}) {
        tx.parts=p;
        assert(parseZipRecoveryTransaction(serializeZipRecoveryTransaction(tx),job,parsed));
        assert(parsed.source==tx.source && parsed.base==tx.base && parsed.parts==p);
        assert(parsed.info==tx.info);
    }
    assert(savedZipPartPath(tx.base+".001",3)==tx.base+".004");
    tx.parts=3;
    assert(!parseZipRecoveryTransaction(serializeZipRecoveryTransaction(tx),job,parsed));
    tx.parts=2; tx.base="ux0:data/psvitaalive/manual/../../bad.zip";
    assert(!parseZipRecoveryTransaction(serializeZipRecoveryTransaction(tx),job,parsed));
    tx.base="ux0:data/psvitaalive/manual/PCSA12345/payload.zip";
    tx.source="ux0:data/wrong/payload.zip";
    assert(!parseZipRecoveryTransaction(serializeZipRecoveryTransaction(tx),job,parsed));
    assert(!parseZipRecoveryTransaction("ZIP recovery pending",job,parsed));
    return 0;
}
