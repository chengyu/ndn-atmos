/** NDN-Atmos: Cataloging Service for distributed data originally developed
 *  for atmospheric science data
 *  Copyright (C) 2015 Colorado State University
 *
 *  NDN-Atmos is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  NDN-Atmos is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NDN-Atmos.  If not, see <http://www.gnu.org/licenses/>.
**/

#ifndef ATMOS_QUERY_QUERY_ADAPTER_HPP
#define ATMOS_QUERY_QUERY_ADAPTER_HPP

#include "util/catalog-adapter.hpp"
#include "util/mysql-util.hpp"
#include "util/config-file.hpp"

#include <thread>

#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>

#include <ndn-cxx/data.hpp>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/interest-filter.hpp>
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/util/time.hpp>
#include <ndn-cxx/encoding/encoding-buffer.hpp>
#include <ndn-cxx/util/in-memory-storage-lru.hpp>

#include "mysql/mysql.h"

#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

namespace atmos {
namespace query {
static const size_t MAX_SEGMENT_SIZE = ndn::MAX_NDN_PACKET_SIZE >> 1;

/**
 * QueryAdapter handles the Query usecases for the catalog
 */
template <typename DatabaseHandler>
class QueryAdapter : public atmos::util::CatalogAdapter {
public:
  /**
   * Constructor
   *
   * @param face:      Face that will be used for NDN communications
   * @param keyChain:  KeyChain that will be used for data signing
   */
  QueryAdapter(const std::shared_ptr<ndn::Face>& face,
               const std::shared_ptr<ndn::KeyChain>& keyChain);

  virtual
  ~QueryAdapter();

  /**
   * Helper function to specify section handler
   */
  void
  setConfigFile(util::ConfigFile& config,
                const ndn::Name& prefix);

protected:
  /**
   * Helper function for configuration parsing
   */
  void
  onConfig(const util::ConfigSection& section,
           bool isDryDun,
           const std::string& fileName,
           const ndn::Name& prefix);

  /**
   * Handles incoming query requests by stripping the filter off the Interest to get the
   * actual request out. This removes the need for a 2-step Interest-Data retrieval.
   *
   * @param filter:   InterestFilter that caused this Interest to be routed
   * @param interest: Interest that needs to be handled
   */
  virtual void
  onQueryInterest(const ndn::InterestFilter& filter, const ndn::Interest& interest);

  /**
   * Handles requests for responses to an existing query
   *
   * @param filter:   InterestFilter that caused this Interest to be routed
   * @param interest: Interest that needs to be handled
   */
  virtual void
  onQueryResultsInterest(const ndn::InterestFilter& filter, const ndn::Interest& interest);

  /**
   * Helper function that makes query-results data
   *
   * @param segmentPrefix:  Name that identifies the Prefix for the Data
   * @param value:          Json::Value to be sent in the Data
   * @param segmentNo:      uint64_t the segment for this Data
   * @param isFinalBlock:   bool to indicate whether this needs to be flagged in the Data as the
   *                         last entry
   * @param isAutocomplete: bool to indicate whether this is an autocomplete message
   */
  std::shared_ptr<ndn::Data>
  makeReplyData(const ndn::Name& segmentPrefix,
                const Json::Value& value,
                uint64_t segmentNo,
                bool isFinalBlock,
                bool isAutocomplete);

  /**
   * Helper function that generates query results from a Json query carried in the Interest
   *
   * @param interest:  Interest that needs to be handled
   */
  void
  runJsonQuery(std::shared_ptr<const ndn::Interest> interest);

  /**
   * Helper function that makes ACK data
   *
   * @param interest: Intersts that needs to be handled
   * @param version:  Version that needs to be in the data name
   */
  std::shared_ptr<ndn::Data>
  makeAckData(std::shared_ptr<const ndn::Interest> interest,
              const ndn::Name::Component& version);

  /**
   * Helper function that generates the sqlQuery string and autocomplete flag
   * @param sqlQuery:     stringstream to save the sqlQuery string
   * @param jsonValue:    Json value that contains the query information
   * @param autocomplete: Flag to indicate if the json contains autocomplete flag
   */
  void
  json2Sql(std::stringstream& sqlQuery,
           Json::Value& jsonValue,
           bool& autocomplete);

  /**
   * Helper function that signs the data
   */
  void
  signData(ndn::Data& data);

  /**
   * Helper function that publishes query-results data segments
   */
  virtual void
  prepareSegments(const ndn::Name& segmentPrefix,
                  const std::string& sqlString,
                  bool autocomplete);

  /**
   * Helper function to set the DatabaseHandler
   */
  void
  setDatabaseHandler(const util::ConnectionDetails&  databaseId);

  /**
   * Helper function that set filters to make the adapter work
   */
  void
  setFilters();

protected:
  typedef std::unordered_map<ndn::Name, const ndn::RegisteredPrefixId*> RegisteredPrefixList;
  // Handle to the Catalog's database
  std::shared_ptr<DatabaseHandler> m_databaseHandler;

  // mutex to control critical sections
  std::mutex m_mutex;
  // @{ needs m_mutex protection
  // The Queries we are currently writing to
  std::map<std::string, std::shared_ptr<ndn::Data>> m_activeQueryToFirstResponse;

  ndn::util::InMemoryStorageLru m_cache;
  // @}
  RegisteredPrefixList m_registeredPrefixList;
};

template <typename DatabaseHandler>
QueryAdapter<DatabaseHandler>::QueryAdapter(const std::shared_ptr<ndn::Face>& face,
                                            const std::shared_ptr<ndn::KeyChain>& keyChain)
  : util::CatalogAdapter(face, keyChain)
  , m_cache(250000)
{
}

template <typename DatabaseHandler>
void
QueryAdapter<DatabaseHandler>::setFilters()
{
  ndn::Name queryPrefix = ndn::Name(m_prefix).append("query");
  m_registeredPrefixList[queryPrefix] = m_face->setInterestFilter(ndn::InterestFilter(queryPrefix),
                            bind(&query::QueryAdapter<DatabaseHandler>::onQueryInterest,
                                 this, _1, _2),
                            bind(&query::QueryAdapter<DatabaseHandler>::onRegisterSuccess,
                                 this, _1),
                            bind(&query::QueryAdapter<DatabaseHandler>::onRegisterFailure,
                                 this, _1, _2));

  ndn::Name resultPrefix = ndn::Name(m_prefix).append("query-results");
  m_registeredPrefixList[resultPrefix] = m_face->setInterestFilter(ndn::InterestFilter(ndn::Name(m_prefix).append("query-results")),
                            bind(&query::QueryAdapter<DatabaseHandler>::onQueryResultsInterest,
                                 this, _1, _2),
                            bind(&query::QueryAdapter<DatabaseHandler>::onRegisterSuccess,
                                 this, _1),
                            bind(&query::QueryAdapter<DatabaseHandler>::onRegisterFailure,
                                 this, _1, _2));
}

template <typename DatabaseHandler>
void
QueryAdapter<DatabaseHandler>::setConfigFile(util::ConfigFile& config,
                                             const ndn::Name& prefix)
{
  config.addSectionHandler("queryAdapter", bind(&QueryAdapter<DatabaseHandler>::onConfig, this,
                                                _1, _2, _3, prefix));
}

template <typename DatabaseHandler>
void
QueryAdapter<DatabaseHandler>::onConfig(const util::ConfigSection& section,
                                        bool isDryRun,
                                        const std::string& filename,
                                        const ndn::Name& prefix)
{
  using namespace util;
  if (isDryRun) {
    return;
  }
  std::string signingId, dbServer, dbName, dbUser, dbPasswd;
  for (auto item = section.begin();
       item != section.end();
       ++ item)
  {
    if (item->first == "signingId") {
      signingId.assign(item->second.get_value<std::string>());
      if (signingId.empty()) {
        throw Error("Empty value for \"signingId\""
                                " in \"query\" section");
      }
    }
    if (item->first == "database") {
      const util::ConfigSection& dataSection = item->second;
      for (auto subItem = dataSection.begin();
           subItem != dataSection.end();
           ++ subItem)
      {
        if (subItem->first == "dbServer") {
          dbServer.assign(subItem->second.get_value<std::string>());
          if (dbServer.empty()){
            throw Error("Invalid value for \"dbServer\""
                                    " in \"query\" section");
          }
        }
        if (subItem->first == "dbName") {
          dbName.assign(subItem->second.get_value<std::string>());
          if (dbName.empty()){
            throw Error("Invalid value for \"dbName\""
                                    " in \"query\" section");
          }
        }
        if (subItem->first == "dbUser") {
          dbUser.assign(subItem->second.get_value<std::string>());
          if (dbUser.empty()){
            throw Error("Invalid value for \"dbUser\""
                                    " in \"query\" section");
          }
        }
        if (subItem->first == "dbPasswd") {
          dbPasswd.assign(subItem->second.get_value<std::string>());
          if (dbPasswd.empty()){
            throw Error("Invalid value for \"dbPasswd\""
                                    " in \"query\" section");
          }
        }
      }
    }
  }

  m_prefix = prefix;
  m_signingId = ndn::Name(signingId);
  util::ConnectionDetails mysqlId(dbServer, dbUser, dbPasswd, dbName);

  setDatabaseHandler(mysqlId);
  setFilters();
}

template <typename DatabaseHandler>
void
QueryAdapter<DatabaseHandler>::setDatabaseHandler(const util::ConnectionDetails& databaseId)
{
  //empty
}

template <>
void
QueryAdapter<MYSQL>::setDatabaseHandler(const util::ConnectionDetails& databaseId)
{
  std::shared_ptr<MYSQL> conn = atmos::util::MySQLConnectionSetup(databaseId);

  m_databaseHandler = conn;
}

template <typename DatabaseHandler>
QueryAdapter<DatabaseHandler>::~QueryAdapter()
{
  for (const auto& itr : m_registeredPrefixList) {
    if (static_cast<bool>(itr.second))
      m_face->unsetInterestFilter(itr.second);
  }
}

template <typename DatabaseHandler>
void
QueryAdapter<DatabaseHandler>::onQueryInterest(const ndn::InterestFilter& filter,
                                               const ndn::Interest& interest)
{
  // strictly enforce query initialization namespace.
  // Name should be our local prefix + "query" + parameters
  if (interest.getName().size() != filter.getPrefix().size() + 1) {
    // @todo: return a nack
    return;
  }
  std::shared_ptr<const ndn::Interest> interestPtr = interest.shared_from_this();
  #ifndef NDEBUG
    std::cout << "query interest : " << interestPtr->getName() << std::endl;
  #endif
  // @todo: use thread pool
  std::thread queryThread(&QueryAdapter<DatabaseHandler>::runJsonQuery,
                          this,
                          interestPtr);
  queryThread.join();
}

template <typename DatabaseHandler>
void
QueryAdapter<DatabaseHandler>::onQueryResultsInterest(const ndn::InterestFilter& filter,
                                                      const ndn::Interest& interest)
{
  // FIXME Results are currently getting served out of the forwarder's
  // CS so we just ignore any retrieval Interests that hit us for
  // now. In the future, this should check some form of
  // InMemoryStorage.
  #ifndef NDEBUG
    std::cout << "query results interest : " << interest.toUri() << std::endl;
  #endif
  auto data = m_cache.find(interest.getName());
  if (data) {
    m_face->put(*data);
  }
}

template <typename DatabaseHandler>
void
QueryAdapter<DatabaseHandler>::signData(ndn::Data& data)
{
  if (m_signingId.empty())
    m_keyChain->sign(data);
  else {
    ndn::Name keyName = m_keyChain->getDefaultKeyNameForIdentity(m_signingId);
    ndn::Name certName = m_keyChain->getDefaultCertificateNameForKey(keyName);
    m_keyChain->sign(data, certName);
  }
}

template <typename DatabaseHandler>
std::shared_ptr<ndn::Data>
QueryAdapter<DatabaseHandler>::makeAckData(std::shared_ptr<const ndn::Interest> interest,
                                           const ndn::Name::Component& version)
{
  // JSON parsed ok, so we can acknowledge successful receipt of the query
  ndn::Name ackName(interest->getName());
  ackName.append(version);
  ackName.append("OK");

  std::shared_ptr<ndn::Data> ack = std::make_shared<ndn::Data>(ackName);
  signData(*ack);
  #ifndef NDEBUG
    std::cout << "makeAckData : " << ackName << std::endl;
  #endif
  return ack;
}

template <typename DatabaseHandler>
void
QueryAdapter<DatabaseHandler>::json2Sql(std::stringstream& sqlQuery,
                                        Json::Value& jsonValue,
                                        bool& autocomplete)
{
  // 3) Convert the JSON Query into a MySQL one
  sqlQuery << "SELECT name FROM cmip5";
  bool input = false;
  for (Json::Value::iterator iter = jsonValue.begin(); iter != jsonValue.end(); ++iter)
  {
    Json::Value key = iter.key();
    Json::Value value = (*iter);

    if (input) {
      sqlQuery << " AND";
    } else {
      sqlQuery << " WHERE";
    }

    // Auto-complete case
    if (key.asString().compare("?") == 0) {
      sqlQuery << " name REGEXP '^" << value.asString() << "'";
      autocomplete = true;
    }
    // Component case
    else {
      sqlQuery << " " << key.asString() << "='" << value.asString() << "'";
    }
    input = true;
  }

  if (!input) { // Force it to be the empty set
    sqlQuery << " limit 0";
  }
  sqlQuery << ";";
}

template <typename DatabaseHandler>
void
QueryAdapter<DatabaseHandler>::runJsonQuery(std::shared_ptr<const ndn::Interest> interest)
{
  // 1) Strip the prefix off the ndn::Interest's ndn::Name
  // +1 to grab JSON component after "query" component

  ndn::Name::Component jsonStr = interest->getName()[m_prefix.size()+1];
  // This one cannot parse the JsonQuery correctly, and should be moved to runJsonQuery
  const std::string jsonQuery(reinterpret_cast<const char*>(jsonStr.value()), jsonStr.value_size());

  if (jsonQuery.length() <= 0) {
    // send Nack?
    return;
  }
  // ------------------
  // For efficiency, do a double check. Once without the lock, then with it.
  if (m_activeQueryToFirstResponse.find(jsonQuery) != m_activeQueryToFirstResponse.end()) {
    m_mutex.lock();
    { // !!! BEGIN CRITICAL SECTION !!!
      // If this fails upon locking, we removed it during our search.
      // An unusual race-condition case, which requires things like PIT aggregation to be off.
      auto iter = m_activeQueryToFirstResponse.find(jsonQuery);
      if (iter != m_activeQueryToFirstResponse.end()) {
        m_face->put(*(iter->second));
        m_mutex.unlock(); //escape lock
        return;
      }
    } // !!!  END  CRITICAL SECTION !!!
    m_mutex.unlock();
  }

  // 2) From the remainder of the ndn::Interest's ndn::Name, get the JSON out
  Json::Value parsedFromString;
  Json::Reader reader;
  if (!reader.parse(jsonQuery, parsedFromString)) {
    // @todo: send NACK?
    std::cout << "cannot parse the JsonQuery" << std::endl;
    return;
  }

  const ndn::name::Component version
    = ndn::name::Component::fromVersion(ndn::time::toUnixTimestamp(
                                          ndn::time::system_clock::now()).count());

  std::shared_ptr<ndn::Data> ack = makeAckData(interest, version);

  m_mutex.lock();
  { // !!! BEGIN CRITICAL SECTION !!!
    // An unusual race-condition case, which requires things like PIT aggregation to be off.
    auto iter = m_activeQueryToFirstResponse.find(jsonQuery);
    if (iter != m_activeQueryToFirstResponse.end()) {
      m_face->put(*(iter->second));
      m_mutex.unlock(); // escape lock
      return;
    }
    // This is where things are expensive so we save them for the lock
    m_activeQueryToFirstResponse.insert(std::pair<std::string,
                                        std::shared_ptr<ndn::Data>>(jsonQuery, ack));
    m_face->put(*ack);
  } // !!!  END  CRITICAL SECTION !!!
  m_mutex.unlock();

  // 3) Convert the JSON Query into a MySQL one
  bool autocomplete = false;
  std::stringstream sqlQuery;
  json2Sql(sqlQuery, parsedFromString, autocomplete);

  // 4) Run the Query
  ndn::Name segmentPrefix(m_prefix);
  segmentPrefix.append("query-results");
  segmentPrefix.append(version);

  prepareSegments(segmentPrefix, sqlQuery.str(), autocomplete);
}

template <typename DatabaseHandler>
void
QueryAdapter<DatabaseHandler>::prepareSegments(const ndn::Name& segmentPrefix,
                                               const std::string& sqlString,
                                               bool autocomplete)
{
  // empty
}

// prepareSegments specilization function
template<>
void
QueryAdapter<MYSQL>::prepareSegments(const ndn::Name& segmentPrefix,
                                     const std::string& sqlString,
                                     bool autocomplete)
{
#ifndef NDEBUG
  std::cout << "sqlString in prepareSegments : " << sqlString << std::endl;
#endif
  // 4) Run the Query
  std::shared_ptr<MYSQL_RES> results
    = atmos::util::MySQLPerformQuery(m_databaseHandler, sqlString);

  if (!results) {
#ifndef NDEBUG
    std::cout << "null MYSQL_RES for query : " << sqlString << std::endl;
#endif
    // @todo: throw runtime error or log the error message?
    return;
  }

#ifndef NDEBUG
  std::cout << "Query results for \""
            << sqlString
            << "\" contain "
            << mysql_num_rows(results.get())
            << " rows" << std::endl;
#endif

  MYSQL_ROW row;
  size_t usedBytes = 0;
  const size_t PAYLOAD_LIMIT = 7000;
  uint64_t segmentNo = 0;
  Json::Value array;
  while ((row = mysql_fetch_row(results.get())))
  {
    size_t size = strlen(row[0]) + 1;
    if (usedBytes + size > PAYLOAD_LIMIT) {
      std::shared_ptr<ndn::Data> data
        = makeReplyData(segmentPrefix, array, segmentNo, false, autocomplete);
      m_mutex.lock();
      m_cache.insert(*data);
      m_mutex.unlock();
      array.clear();
      usedBytes = 0;
      segmentNo++;
    }
    array.append(row[0]);
    usedBytes += size;
  }
  std::shared_ptr<ndn::Data> data
    = makeReplyData(segmentPrefix, array, segmentNo, true, autocomplete);
  m_mutex.lock();
  m_cache.insert(*data);
  m_mutex.unlock();
}

template <typename DatabaseHandler>
std::shared_ptr<ndn::Data>
QueryAdapter<DatabaseHandler>::makeReplyData(const ndn::Name& segmentPrefix,
                                             const Json::Value& value,
                                             uint64_t segmentNo,
                                             bool isFinalBlock,
                                             bool isAutocomplete)
{
  Json::Value entry;
  Json::FastWriter fastWriter;
  if (isAutocomplete) {
    entry["next"] = value;
  } else {
    entry["results"] = value;
  }
  const std::string jsonMessage = fastWriter.write(entry);
  const char* payload = jsonMessage.c_str();
  size_t payloadLength = jsonMessage.size() + 1;
  ndn::Name segmentName(segmentPrefix);
  segmentName.appendSegment(segmentNo);

  std::shared_ptr<ndn::Data> data = std::make_shared<ndn::Data>(segmentName);
  data->setContent(reinterpret_cast<const uint8_t*>(payload), payloadLength);
  data->setFreshnessPeriod(ndn::time::milliseconds(10000));

  if (isFinalBlock) {
    data->setFinalBlockId(ndn::Name::Component::fromSegment(segmentNo));
  }
#ifndef NDEBUG
  std::cout << "makeReplyData : " << segmentName << std::endl;
#endif
  signData(*data);
  return data;
}

} // namespace query
} // namespace atmos
#endif //ATMOS_QUERY_QUERY_ADAPTER_HPP
