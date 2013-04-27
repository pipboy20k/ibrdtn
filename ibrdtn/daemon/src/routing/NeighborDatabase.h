/*
 * NeighborDatabase.h
 *
 * Copyright (C) 2011 IBR, TU Braunschweig
 *
 * Written-by: Johannes Morgenroth <morgenroth@ibr.cs.tu-bs.de>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef NEIGHBORDATABASE_H_
#define NEIGHBORDATABASE_H_

#include "routing/NeighborDataset.h"
#include <ibrdtn/data/BundleSet.h>
#include <ibrdtn/data/EID.h>
#include <ibrdtn/data/BundleID.h>
#include <ibrcommon/data/BloomFilter.h>
#include <ibrcommon/Exceptions.h>
#include <ibrcommon/thread/ThreadsafeState.h>
#include <algorithm>
#include <map>

namespace dtn
{
	namespace routing
	{
		/**
		 * The neighbor database contains collected information about neighbors.
		 * This includes the last timestamp on which a neighbor was seen, the bundles
		 * this neighbors has received (bloomfilter with age).
		 */
		class NeighborDatabase : public ibrcommon::Mutex
		{
		public:
			class BloomfilterNotAvailableException : public ibrcommon::Exception
			{
			public:
				BloomfilterNotAvailableException(const dtn::data::EID &host)
				: ibrcommon::Exception("Bloom filter is not available for this node."), eid(host) { };

				virtual ~BloomfilterNotAvailableException() throw () { };

				const dtn::data::EID eid;
			};

			class NoMoreTransfersAvailable : public ibrcommon::Exception
			{
			public:
				NoMoreTransfersAvailable() : ibrcommon::Exception("No more transfers allowed.") { };
				virtual ~NoMoreTransfersAvailable() throw () { };
			};

			class AlreadyInTransitException : public ibrcommon::Exception
			{
			public:
				AlreadyInTransitException() : ibrcommon::Exception("This bundle is already in transit.") { };
				virtual ~AlreadyInTransitException() throw () { };
			};

			class NeighborNotAvailableException : public ibrcommon::Exception
			{
			public:
				NeighborNotAvailableException() : ibrcommon::Exception("Entry for this neighbor not found.") { };
				virtual ~NeighborNotAvailableException() throw () { };
			};

			class DatasetNotAvailableException : public ibrcommon::Exception
			{
			public:
				DatasetNotAvailableException() : ibrcommon::Exception("Dataset not found.") { };
				virtual ~DatasetNotAvailableException() throw () { };
			};

			class NeighborEntry
			{
			public:
				NeighborEntry();
				NeighborEntry(const dtn::data::EID &eid);
				virtual ~NeighborEntry();

				/**
				 * updates the bloomfilter of this entry with a new one
				 * @param bf The bloomfilter object
				 * @param lifetime The desired lifetime of this bloomfilter
				 */
				void update(const ibrcommon::BloomFilter &bf, const uint64_t lifetime = 0);

				void reset();

				void add(const dtn::data::MetaBundle&);

				bool has(const dtn::data::BundleID&, const bool require_bloomfilter = false) const;

				/**
				 * acquire resource to send a filter request.
				 * The resources are reset once if the filter expires.
				 */
				void acquireFilterRequest() throw (NoMoreTransfersAvailable);

				/**
				 * Acquire transfer resources. If no resources is left,
				 * an exception is thrown.
				 */
				void acquireTransfer(const dtn::data::BundleID &id) throw (NoMoreTransfersAvailable, AlreadyInTransitException);

				/**
				 * @return the number of free transfer slots
				 */
				size_t getFreeTransferSlots() const;
				
				/**
				 * @return True, if the threshold of free transfer slots is reached.
				 */
				bool isTransferThresholdReached() const;

				/**
				 * Release a transfer resource, but never exceed the maxium
				 * resource limit.
				 */
				void releaseTransfer(const dtn::data::BundleID &id);

				// the EID of the corresponding node
				const dtn::data::EID eid;

				/**
				 * trigger expire mechanisms for bloomfilter and bundle summary
				 * @param timestamp
				 */
				void expire(const size_t timestamp);

				/**
				 * Retrieve a specific data-set.
				 */
				template <class T>
				const T& getDataset() const throw (DatasetNotAvailableException)
				{
					NeighborDataset item(T::identifier);
					data_set::const_iterator iter = _datasets.find(item);

					if (iter == _datasets.end()) throw DatasetNotAvailableException();

					try {
						return dynamic_cast<const T&>(**iter);
					} catch (const std::bad_cast&) {
						throw DatasetNotAvailableException();
					}
				}

				/**
				 * Put a data-set into the entry.
				 * The old data-set gets replaced.
				 */
				void putDataset(NeighborDataset &dset);

				/**
				 * Remove a data-set.
				 */
				template <class T>
				void removeDataset()
				{
					NeighborDataset item(T::identifier);
					data_set::iterator it = _datasets.find(item);

					if (it == _datasets.end()) return;

					_datasets.erase(it);
				}

			private:
				// stores bundle currently in transit
				ibrcommon::Mutex _transit_lock;
				std::set<dtn::data::BundleID> _transit_bundles;

				// bloomfilter used as summary vector
				ibrcommon::BloomFilter _filter;
				dtn::data::BundleSet _summary;
				uint64_t _filter_expire;

				// extended neighbor data
				typedef std::set<NeighborDataset> data_set;
				data_set _datasets;

				enum FILTER_REQUEST_STATE
				{
					FILTER_AWAITING = 0,
					FILTER_AVAILABLE = 1,
					FILTER_EXPIRED = 2,
					FILTER_FINAL = 3
				};

				ibrcommon::ThreadsafeState<FILTER_REQUEST_STATE> _filter_state;
			};

			NeighborDatabase();
			virtual ~NeighborDatabase();

			/**
			 * Query a neighbor entry of the database. It throws an exception
			 * if the neighbor is not available.
			 * @param eid The EID of the neighbor
			 * @return The neighbor entry reference.
			 */
			NeighborDatabase::NeighborEntry& get(const dtn::data::EID &eid) throw (NeighborNotAvailableException);

			/**
			 * Query a neighbor entry of the database. If the entry does not
			 * exists, a new entry is created and returned.
			 * @param eid The EID of the neighbor.
			 * @return The neighbor entry reference.
			 */
			NeighborDatabase::NeighborEntry& create(const dtn::data::EID &eid) throw ();

			/**
			 * Remove an entry of the database.
			 * @param eid The EID of the neighbor to remove.
			 */
			void remove(const dtn::data::EID &eid);

			/**
			 * trigger expire mechanisms for bloomfilter and bundle summary
			 * @param timestamp
			 */
			void expire(const size_t timestamp);

		private:
			typedef std::map<dtn::data::EID, NeighborDatabase::NeighborEntry* > neighbor_map;
			neighbor_map _entries;
		};
	}
}

#endif /* NEIGHBORDATABASE_H_ */
