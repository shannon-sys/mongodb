/**
 *    Copyright (C) 2014 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include <sstream>

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include "rocks_server_status.h"

#include "swift/shannon_db.h"

#include "mongo/base/checked_cast.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

#include "rocks_recovery_unit.h"
#include "rocks_engine.h"
#include "rocks_transaction.h"

namespace mongo {
    using std::string;

    namespace {
        std::string PrettyPrintBytes(size_t bytes) {
            if (bytes < (16 << 10)) {
                return std::to_string(bytes) + "B";
            } else if (bytes < (16 << 20)) {
                return std::to_string(bytes >> 10) + "KB";
            } else if (bytes < (16LL << 30)) {
                return std::to_string(bytes >> 20) + "MB";
            } else {
                return std::to_string(bytes >> 30) + "GB";
            }
        }
    }  // namespace

    RocksServerStatusSection::RocksServerStatusSection(RocksEngine* engine)
        : ServerStatusSection("rocksdb"), _engine(engine) {}

    bool RocksServerStatusSection::includeByDefault() const { return true; }

    BSONObj RocksServerStatusSection::generateSection(OperationContext* opCtx,
                                                      const BSONElement& configElement) const {
        Lock::GlobalLock lk(opCtx, LockMode::MODE_IS, UINT_MAX);

        BSONObjBuilder bob;

        // if the second is true, that means that we pass the value through PrettyPrintBytes
        std::vector<std::pair<std::string, bool>> properties = {
            {"stats", false},
            {"num-immutable-mem-table", false},
            {"mem-table-flush-pending", false},
            {"compaction-pending", false},
            {"background-errors", false},
            {"cur-size-active-mem-table", true},
            {"cur-size-all-mem-tables", true},
            {"num-entries-active-mem-table", false},
            {"num-entries-imm-mem-tables", false},
            {"estimate-table-readers-mem", true},
            {"num-snapshots", false},
            {"oldest-snapshot-time", false},
            {"num-live-versions", false}};
        for (auto const& property : properties) {
            std::string statsString;
            if (!_engine->getDB()->GetProperty("rocksdb." + property.first, &statsString)) {
                statsString = "<error> unable to retrieve statistics";
                bob.append(property.first, statsString);
                continue;
            }
            if (property.first == "stats") {
                // special casing because we want to turn it into array
                BSONArrayBuilder a;
                std::stringstream ss(statsString);
                std::string line;
                while (std::getline(ss, line)) {
                    a.append(line);
                }
                bob.appendArray(property.first, a.arr());
            } else if (property.second) {
                //try {
                //    bob.append(property.first, PrettyPrintBytes(std::stoll(statsString)));
                //} catch(std::invalid_argument)
                //{
                //    log() << "invalid std::stoll(statsString) [" << statsString << "]";
                //}
            } else {
                bob.append(property.first, statsString);
            }
        }
        bob.append("total-live-recovery-units", RocksRecoveryUnit::getTotalLiveRecoveryUnits());
        //bob.append("block-cache-usage", PrettyPrintBytes(_engine->getBlockCacheUsage()));
        bob.append("transaction-engine-keys",
                   static_cast<long long>(_engine->getTransactionEngine()->numKeysTracked()));
        bob.append("transaction-engine-snapshots",
                   static_cast<long long>(_engine->getTransactionEngine()->numActiveSnapshots()));

        /*
        std::vector<shannon::ThreadStatus> threadList;
        auto s = shannon::Env::Default()->GetThreadList(&threadList);
        if (s.ok()) {
            BSONArrayBuilder threadStatus;
            for (auto& ts : threadList) {
                if (ts.operation_type == shannon::ThreadStatus::OP_UNKNOWN ||
                    ts.thread_type == shannon::ThreadStatus::USER) {
                    continue;
                }
                BSONObjBuilder threadObjBuilder;
                threadObjBuilder.append("type",
                                        shannon::ThreadStatus::GetOperationName(ts.operation_type));
                threadObjBuilder.append(
                    "time_elapsed", shannon::ThreadStatus::MicrosToString(ts.op_elapsed_micros));
                auto op_properties = shannon::ThreadStatus::InterpretOperationProperties(
                    ts.operation_type, ts.op_properties);

                const std::vector<std::pair<std::string, std::string>> properties(
                    {{"JobID", "job_id"},
                     {"BaseInputLevel", "input_level"},
                     {"OutputLevel", "output_level"},
                     {"IsManual", "manual"}});
                for (const auto& prop : properties) {
                    auto itr = op_properties.find(prop.first);
                    if (itr != op_properties.end()) {
                        threadObjBuilder.append(prop.second, static_cast<int>(itr->second));
                    }
                }

                const std::vector<std::pair<std::string, std::string>> byte_properties(
                    {{"BytesRead", "bytes_read"}, {"BytesWritten", "bytes_written"},
                     {"TotalInputBytes", "total_bytes"}});
                for (const auto& prop : byte_properties) {
                    auto itr = op_properties.find(prop.first);
                    if (itr != op_properties.end()) {
                        threadObjBuilder.append(prop.second,
                                                PrettyPrintBytes(static_cast<size_t>(itr->second)));
                    }
                }

                const std::vector<std::pair<std::string, std::string>> speed_properties(
                    {{"BytesRead", "read_throughput"}, {"BytesWritten", "write_throughput"}});
                for (const auto& prop : speed_properties) {
                    auto itr = op_properties.find(prop.first);
                    if (itr != op_properties.end()) {
                        size_t speed = (itr->second * 1000 * 1000) /
                                       static_cast<size_t>(
                                           (ts.op_elapsed_micros == 0) ? 1 : ts.op_elapsed_micros);
                        threadObjBuilder.append(
                            prop.second, PrettyPrintBytes(static_cast<size_t>(speed)) + "/s");
                    }
                }

                threadStatus.append(threadObjBuilder.obj());
            }
            bob.appendArray("thread-status", threadStatus.arr());
        }
        */
        // add counters
        /* disable Statistics
        auto stats = _engine->getStatistics();
        if (stats) {
          BSONObjBuilder countersObjBuilder;
          const std::vector<std::pair<shannon::Tickers, std::string>> counterNameMap = {
            {shannon::NUMBER_KEYS_WRITTEN, "num-keys-written"},
            {shannon::NUMBER_KEYS_READ, "num-keys-read"},
            {shannon::NUMBER_DB_SEEK, "num-seeks"},
            {shannon::NUMBER_DB_NEXT, "num-forward-iterations"},
            {shannon::NUMBER_DB_PREV, "num-backward-iterations"},
            {shannon::BLOCK_CACHE_MISS, "block-cache-misses"},
            {shannon::BLOCK_CACHE_HIT, "block-cache-hits"},
            {shannon::BLOOM_FILTER_USEFUL, "bloom-filter-useful"},
            {shannon::BYTES_WRITTEN, "bytes-written"},
            {shannon::BYTES_READ, "bytes-read-point-lookup"},
            {shannon::ITER_BYTES_READ, "bytes-read-iteration"},
            {shannon::FLUSH_WRITE_BYTES, "flush-bytes-written"},
            {shannon::COMPACT_READ_BYTES, "compaction-bytes-read"},
            {shannon::COMPACT_WRITE_BYTES, "compaction-bytes-written"}
          };

          for (const auto& counter_name : counterNameMap) {
            countersObjBuilder.append(
                counter_name.second,
                static_cast<long long>(stats->getTickerCount(counter_name.first)));
          }

          bob.append("counters", countersObjBuilder.obj());
        }
        */

        RocksEngine::appendGlobalStats(bob);

        return bob.obj();
    }

}  // namespace mongo

