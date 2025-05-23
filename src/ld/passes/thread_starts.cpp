/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2009 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */


#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <dlfcn.h>
#include <libkern/OSByteOrder.h>

#include <algorithm>
#include <vector>
#include <map>

#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"
#include "ld.hpp"
#include "thread_starts.h"

namespace ld {
namespace passes {
namespace thread_starts {


static std::map<const Atom*, uint64_t> sAtomToAddress;




class ThreadStartsAtom : public ld::Atom {
public:
											ThreadStartsAtom(uint32_t fixupAlignment, uint32_t numThreadStarts)
											  : ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
												ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified,
												symbolTableNotIn, false, false, false, ld::Atom::Alignment(2)),
												_fixupAlignment(fixupAlignment), _numThreadStarts(numThreadStarts)
											{
												assert(_fixupAlignment == 4 || _fixupAlignment == 8);
											}

	virtual const ld::File*					file() const					{ return NULL; }
	virtual const char*						name() const					{ return "thread starts"; }
	virtual uint64_t						size() const					{ return 4 + (_numThreadStarts * 4); }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		uint32_t header = 0;
		if (_fixupAlignment == 8)
			header |= 1;
		bzero(buffer, size());
		*((uint32_t*)(&buffer[0])) = header; // header
		// Fill in offsets with 0xFFFFFFFF's for now as that wouldn't be a valid offset
		memset(&buffer[4], 0xFFFFFFFF, _numThreadStarts * sizeof(uint32_t));
	}
	virtual void							setScope(Scope)					{ }
	virtual Fixup::iterator					fixupsBegin() const	{ return NULL; }
	virtual Fixup::iterator					fixupsEnd() const	{ return NULL; }

private:

	uint32_t								_fixupAlignment;
	uint32_t								_numThreadStarts;

	static ld::Section						_s_section;
};

ld::Section ThreadStartsAtom::_s_section("__TEXT", "__thread_starts", ld::Section::typeThreadStarts);




class ChainStartsAtom : public ld::Atom {
public:
											ChainStartsAtom(uint32_t chainStartsCount)
											  : ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
												ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified,
												symbolTableNotIn, false, false, false, ld::Atom::Alignment(2)),
												_chainStartsCount(chainStartsCount)
											{
											}

	virtual const ld::File*					file() const					{ return NULL; }
	virtual const char*						name() const					{ return "chain starts"; }
	virtual uint64_t						size() const					{ return offsetof(dyld_chained_starts_offsets, chain_starts[_chainStartsCount]); }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const { }
	virtual void							setScope(Scope)					{ }
	virtual Fixup::iterator					fixupsBegin() const	{ return NULL; }
	virtual Fixup::iterator					fixupsEnd() const	{ return NULL; }

private:

	uint32_t								_chainStartsCount;

	static ld::Section						_s_section;
};

ld::Section ChainStartsAtom::_s_section("__TEXT", "__chain_starts", ld::Section::typeChainStarts);





static void buildAddressMap(const Options& opts, ld::Internal& state) {
	// Assign addresses to sections
	state.setSectionSizesAndAlignments();
	state.assignFileOffsets();

	// Assign addresses to atoms in a side table
	static const bool log = false;
	if ( log ) fprintf(stderr, "buildAddressMap()\n");
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		uint16_t maxAlignment = 0;
		uint64_t offset = 0;
		if ( log ) fprintf(stderr, "  section=%s/%s, address=0x%08llX\n", sect->segmentName(), sect->sectionName(), sect->address);
		for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
			const ld::Atom* atom = *ait;
			uint32_t atomAlignmentPowerOf2 = atom->alignment().powerOf2;
			uint32_t atomModulus = atom->alignment().modulus;
			if ( atomAlignmentPowerOf2 > maxAlignment )
				maxAlignment = atomAlignmentPowerOf2;
			// calculate section offset for this atom
			uint64_t alignment = 1 << atomAlignmentPowerOf2;
			uint64_t currentModulus = (offset % alignment);
			uint64_t requiredModulus = atomModulus;
			if ( currentModulus != requiredModulus ) {
				if ( requiredModulus > currentModulus )
					offset += requiredModulus-currentModulus;
				else
					offset += requiredModulus+alignment-currentModulus;
			}

			if ( log ) fprintf(stderr, "    0x%08llX atom=%p, name=%s\n", sect->address+offset, atom, atom->name());
			sAtomToAddress[atom] = sect->address + offset;

			offset += atom->size();
		}
	}
}

static uint32_t threadStartsCountInSection(std::vector<uint64_t>& fixupAddressesInSection) {
	if (fixupAddressesInSection.empty())
		return 0;

	std::sort(fixupAddressesInSection.begin(), fixupAddressesInSection.end());

	uint32_t numThreadStarts = 0;

	uint64_t deltaBits = 11;
	uint64_t minAlignment = 4;
	uint64_t prevAddress = 0;
	for (uint64_t address : fixupAddressesInSection) {
		uint64_t delta = address - prevAddress;
		assert( (delta & (minAlignment - 1)) == 0 );
		delta /= minAlignment;
		if (delta >= (1 << deltaBits)) {
			++numThreadStarts;
		}
		prevAddress = address;
	}
	fixupAddressesInSection.clear();

	return numThreadStarts;
}

static uint32_t processSections(ld::Internal& state, uint64_t minAlignment) {
	uint32_t numThreadStarts = 0;

	std::vector<uint64_t> fixupAddressesInSection;
	for (ld::Internal::FinalSection* sect : state.sections) {
		if ( sect->isSectionHidden() )
			continue;
		for (const ld::Atom* atom : sect->atoms) {
			bool seenTarget = false;
			bool seenSubtractTarget = false;
			bool isPointerStore = false;
			for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
				if ( fit->firstInCluster() ) {
					seenTarget = false;
					seenSubtractTarget = false;
					isPointerStore = false;
				}
				if ( fit->setsTarget(false) )
					seenTarget = true;
				if ( fit->kind == ld::Fixup::kindSubtractTargetAddress )
					seenSubtractTarget = true;
				if ( fit->isStore() ) {
					if ( (fit->kind == ld::Fixup::kindStoreLittleEndian32) || (fit->kind == ld::Fixup::kindStoreLittleEndian64)
#if SUPPORT_ARCH_arm64e
						|| (fit->kind == ld::Fixup::kindStoreLittleEndianAuth64)
#endif
						)
						isPointerStore = true;
					if ( (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndian32) || (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndian64)
#if SUPPORT_ARCH_arm64e
						|| (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndianAuth64)
#endif
						)
						isPointerStore = true;
				}
				if ( fit->isPcRelStore(false) )
					seenSubtractTarget = true;
				if ( fit->lastInCluster()  ) {
					//fprintf(stderr, "fixup at 0x%08llX, seenTarget=%d, seenSubtractTarget=%d, isPointerStore=%d\n", sAtomToAddress[atom] + fit->offsetInAtom,
					//			seenTarget, seenSubtractTarget, isPointerStore);
					if ( seenTarget && !seenSubtractTarget && isPointerStore ) {
						uint64_t address = sAtomToAddress[atom] + fit->offsetInAtom;
						fixupAddressesInSection.push_back(address);
						//fprintf(stderr, "pointer at 0x%08llX\n", address);
						if ( (address & (minAlignment-1)) != 0 ) {
							throwf("pointer not aligned at address 0x%llX (%s + %d from %s)",
								   address, atom->name(), fit->offsetInAtom, atom->safeFilePath());
						}
					}
				}
			}
		}
		numThreadStarts += threadStartsCountInSection(fixupAddressesInSection);
	}

	return numThreadStarts;
}


static uint32_t countChains(ld::Internal& state, uint32_t pointerFormat) {
	uint32_t count = 0;

	uint64_t prevFixupAddress = 0;
	const char* prevFixupSegName = nullptr;
	for (ld::Internal::FinalSection* sect : state.sections) {
		if ( sect->isSectionHidden() )
			continue;
		if ( (prevFixupSegName != nullptr) && (strcmp(prevFixupSegName, sect->segmentName()) != 0) )
			prevFixupAddress = 0;
		for (const ld::Atom* atom : sect->atoms) {
			bool seenTarget = false;
			bool seenSubtractTarget = false;
			bool isPointerStore = false;
			std::vector<uint32_t> atomFixupOffsets;
			for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
				if ( fit->firstInCluster() ) {
					seenTarget = false;
					seenSubtractTarget = false;
					isPointerStore = false;
				}
				if ( fit->setsTarget(false) )
					seenTarget = true;
				if ( fit->kind == ld::Fixup::kindSubtractTargetAddress )
					seenSubtractTarget = true;
				if ( fit->isStore() ) {
					if ( (fit->kind == ld::Fixup::kindStoreLittleEndian32) || (fit->kind == ld::Fixup::kindStoreLittleEndian64)
#if SUPPORT_ARCH_arm64e
						|| (fit->kind == ld::Fixup::kindStoreLittleEndianAuth64)
#endif
						)
						isPointerStore = true;
					if ( (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndian32) || (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndian64)
#if SUPPORT_ARCH_arm64e
						|| (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndianAuth64)
#endif
						)
						isPointerStore = true;
				}
				if ( fit->isPcRelStore(false) )
					seenSubtractTarget = true;
				if ( fit->lastInCluster() ) {
					//fprintf(stderr, "fixup at 0x%08llX, seenTarget=%d, seenSubtractTarget=%d, isPointerStore=%d\n", sAtomToAddress[atom] + fit->offsetInAtom,
					//			seenTarget, seenSubtractTarget, isPointerStore);
					if ( seenTarget && !seenSubtractTarget && isPointerStore ) {
						atomFixupOffsets.push_back(fit->offsetInAtom);
					}
				}
			}
			std::sort(atomFixupOffsets.begin(), atomFixupOffsets.end());
			for (uint32_t offset : atomFixupOffsets ) {
				uint64_t address = sAtomToAddress[atom] + offset;
				//fprintf(stderr, "0x%llX fixup\n", address);
				if ( prevFixupAddress == 0 ) {
					++count;
					//fprintf(stderr, "first chain start at: 0x%llX\n", address-0x7000);
				}
				else {
					uint64_t delta = address - prevFixupAddress;
					if ( delta > 255 ) {
						//fprintf(stderr, "new chain start at: 0x%llX\n", address-0x7000);
						++count;
					}
				}
				prevFixupAddress = address;
				prevFixupSegName = sect->segmentName();
			}
		}
	}

	return count;
}


class RebaseRLEAtom : public ld::Atom {
public:
											RebaseRLEAtom(ld::Internal& state)
											  : ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
												ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified,
												symbolTableNotIn, false, false, false, ld::Atom::Alignment(2)),
												_state(state), _size(0)
											{
											}

	virtual const ld::File*					file() const					{ return nullptr; }
	virtual const char*						name() const					{ return "rebase RLE"; }
	virtual uint64_t						size() const					{ return _size; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
	virtual void							setScope(Scope)					{ }
	virtual Fixup::iterator					fixupsBegin() const	{ return nullptr; }
	virtual Fixup::iterator					fixupsEnd() const	{ return nullptr; }

	void									encode(const std::vector<uint32_t>&) const;
	void									gatherRebases(std::vector<uint32_t>&) const;
	void									computeSize();

private:
	void  									makeRebaseRuns(ld::Internal::FinalSection* sect, std::vector<uint8_t>& runsContent) const;

	ld::Internal& 							_state;
	uint32_t								_size;

	static ld::Section						_s_section;
};

ld::Section RebaseRLEAtom::_s_section("__TEXT", "__rebase_info", ld::Section::typeRebaseRLE);

struct RebaseRuns
{
	uint32_t  startAddress;
	uint8_t   runs[];   // value of even indexes is how many pointers in a row are rebases, value of odd indexes times 4 is memory to skip over
						// two zero values in a row signals the end of the run
};

void RebaseRLEAtom::makeRebaseRuns(ld::Internal::FinalSection* sect, std::vector<uint8_t>& runsContent) const
{
	runsContent.clear();
	uint32_t lastFixupAddress = 0;
	int 	 count            = 0;
	for (const ld::Atom* atom : sect->atoms) {
		bool seenTarget         = false;
		bool seenSubtractTarget = false;
		bool isPointerStore     = false;
		std::vector<uint32_t> atomFixupOffsets;
		for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
			if ( fit->firstInCluster() ) {
				seenTarget         = false;
				seenSubtractTarget = false;
				isPointerStore     = false;
			}
			if ( fit->setsTarget(false) )
				seenTarget = true;
			if ( fit->kind == ld::Fixup::kindSubtractTargetAddress )
				seenSubtractTarget = true;
			if ( fit->isStore() ) {
				if ( (fit->kind == ld::Fixup::kindStoreLittleEndian32) || (fit->kind == ld::Fixup::kindStoreLittleEndian64)
#if SUPPORT_ARCH_arm64e
						|| (fit->kind == ld::Fixup::kindStoreLittleEndianAuth64)
#endif
						)
					isPointerStore = true;
				if ( (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndian32) || (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndian64)
#if SUPPORT_ARCH_arm64e
						|| (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndianAuth64)
#endif
						)
					isPointerStore = true;
			}
			if ( fit->isPcRelStore(false) )
				seenSubtractTarget = true;
			if ( fit->lastInCluster() ) {
				if ( seenTarget && !seenSubtractTarget && isPointerStore ) {
					atomFixupOffsets.push_back(fit->offsetInAtom);
				}
			}
		}
		if ( !atomFixupOffsets.empty() ) {
			std::sort(atomFixupOffsets.begin(), atomFixupOffsets.end());
			for (uint32_t offsetInAtom : atomFixupOffsets ) {
				uint32_t fixupAddress = (atom->finalAddressMode()
										? atom->finalAddress() + offsetInAtom
										: sect->address + atom->sectionOffset() + offsetInAtom);
				if ( (fixupAddress % 4) != 0 )
					throwf("misaligned pointer at 0x%08X in %s\n", fixupAddress, atom->name());
				if ( runsContent.empty() ) {
					runsContent.push_back(0); runsContent.push_back(0); runsContent.push_back(0); runsContent.push_back(0);
					memcpy(&runsContent[0], &fixupAddress, sizeof(uint32_t));
					count = 1;
				}
				else {
					uint32_t delta = fixupAddress - lastFixupAddress;
					if ( delta == 4 ) {
						++count;
					}
					else {
						// found gap, so end previous rebase run
						// break large runs into 255 chunks
						while ( count > 255 )  {
							runsContent.push_back(255);
							runsContent.push_back(0);
							count -= 255;
						}
						runsContent.push_back(count);
						// start a new run
						count = 1;
						if ( delta/4 > 255 ) {
							// gap too large, start new run
							// make sure existing run has 0x00 0x00 termination, then 4-byte align
							if ( runsContent.size() > 4 ) {
								if ( (runsContent[runsContent.size()-1] != 0) || (runsContent[runsContent.size()-2] != 0) ) {
									runsContent.push_back(0);
									runsContent.push_back(0);
								}
							}
							while ( (runsContent.size() % 4) != 0 )
								runsContent.push_back(0);
							size_t curOffset = runsContent.size();
							runsContent.push_back(0); runsContent.push_back(0); runsContent.push_back(0); runsContent.push_back(0);
							memcpy(&runsContent[curOffset], &fixupAddress, sizeof(uint32_t));
						}
						else {
							runsContent.push_back(delta/4);
						}
					}

				}
				lastFixupAddress = fixupAddress;
			}
		}
	}
	if ( count >= 1 ) {
		// add last run
		while ( count > 255 )  {
			runsContent.push_back(255);
			runsContent.push_back(0);
			count -= 255;
		}
		runsContent.push_back(count);
	}
	// add terminator
	if ( !runsContent.empty() ) {
		runsContent.push_back(0);
		runsContent.push_back(0);
		while ( (runsContent.size() % 4) != 0 )
			runsContent.push_back(0);
	}
}

void RebaseRLEAtom::computeSize()
{
	_size = 0;
	std::vector<uint8_t> runsContent;
	for (ld::Internal::FinalSection* sect : _state.sections) {
		if ( sect->isSectionHidden() )
			continue;
		runsContent.clear();
		this->makeRebaseRuns(sect, runsContent);
		_size += runsContent.size();
		//fprintf(stderr, "computeSize: %s/%s size=0x%0lX, totalSize=0x%0X\n", sect->segmentName(), sect->sectionName(), runsContent.size(), _size);
		//printf("");
	}
	//fprintf(stderr, "computeSize: total size=%d\n", _size);
}

void RebaseRLEAtom::copyRawContent(uint8_t buffer[]) const
{
	size_t offsetInBuffer = 0;
	std::vector<uint8_t> runsContent;
	for (ld::Internal::FinalSection* sect : _state.sections) {
		if ( sect->isSectionHidden() )
			continue;
		runsContent.clear();
		this->makeRebaseRuns(sect, runsContent);
		size_t contentSize = runsContent.size();
		memcpy(&buffer[offsetInBuffer], &runsContent[0], contentSize);
		offsetInBuffer += contentSize;
	}
}

void RebaseRLEAtom::gatherRebases(std::vector<uint32_t>& locations) const
{
	uint64_t prevFixupAddress = 0;
	const char* prevFixupSegName = nullptr;
	for (ld::Internal::FinalSection* sect : _state.sections) {
		if ( sect->isSectionHidden() )
			continue;
		if ( (prevFixupSegName != nullptr) && (strcmp(prevFixupSegName, sect->segmentName()) != 0) )
			prevFixupAddress = 0;
		for (const ld::Atom* atom : sect->atoms) {
			bool seenTarget = false;
			bool seenSubtractTarget = false;
			bool isPointerStore = false;
			std::vector<uint32_t> atomFixupOffsets;
			for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
				if ( fit->firstInCluster() ) {
					seenTarget = false;
					seenSubtractTarget = false;
					isPointerStore = false;
				}
				if ( fit->setsTarget(false) )
					seenTarget = true;
				if ( fit->kind == ld::Fixup::kindSubtractTargetAddress )
					seenSubtractTarget = true;
				if ( fit->isStore() ) {
					if ( (fit->kind == ld::Fixup::kindStoreLittleEndian32) || (fit->kind == ld::Fixup::kindStoreLittleEndian64)
#if SUPPORT_ARCH_arm64e
						|| (fit->kind == ld::Fixup::kindStoreLittleEndianAuth64)
#endif
						)
						isPointerStore = true;
					if ( (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndian32) || (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndian64)
#if SUPPORT_ARCH_arm64e
						|| (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndianAuth64)
#endif
						)
						isPointerStore = true;
				}
				if ( fit->isPcRelStore(false) )
					seenSubtractTarget = true;
				if ( fit->lastInCluster() ) {
					//fprintf(stderr, "fixup at 0x%08llX, seenTarget=%d, seenSubtractTarget=%d, isPointerStore=%d\n", sAtomToAddress[atom] + fit->offsetInAtom,
					//			seenTarget, seenSubtractTarget, isPointerStore);
					if ( seenTarget && !seenSubtractTarget && isPointerStore ) {
						uint64_t fixupAddress = sAtomToAddress[atom] + fit->offsetInAtom;
						locations.push_back((uint32_t)fixupAddress);
					}
				}
			}
		}
	}
	std::sort(locations.begin(), locations.end(), [](uint32_t left, uint32_t right) { return (left < right); });
}

void doPass(const Options& opts, ld::Internal& state)
{
	if ( opts.makeThreadedStartsSection() ) {
		buildAddressMap(opts, state);
		uint32_t fixupAlignment = 4;
		uint32_t numThreadStarts = processSections(state, fixupAlignment);
		// create atom that contains the whole chain starts section
		state.addAtom(*new ThreadStartsAtom(fixupAlignment, numThreadStarts));
	}
	else if ( opts.makeChainedFixups() && !opts.dyldOrKernelLoadsOutput() ) {
		buildAddressMap(opts, state);
		uint32_t startsCount = countChains(state, DYLD_CHAINED_PTR_32_FIRMWARE);
		state.addAtom(*new ChainStartsAtom(startsCount));
	}
	else if ( opts.makeRebaseSection() ) {
		RebaseRLEAtom* rebases = new RebaseRLEAtom(state);
		rebases->computeSize();
		state.rebaseRLEAtom = rebases;
		state.addAtom(*state.rebaseRLEAtom);
	}
}


} // namespace thread_starts
} // namespace passes 
} // namespace ld
