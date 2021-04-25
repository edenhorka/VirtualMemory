#include "VirtualMemory.h"
#include "PhysicalMemory.h"


#define SUCCESS 1

#define FAILURE 0

#define READ 2

#define WRITE 3

#define INVALID(virtualAddress, value) ((virtualAddress >= VIRTUAL_MEMORY_SIZE) || (value == nullptr))

#define MIN(a, b) (a) < (b)? (a):(b)

#define GET_BITS(index, num_of_bits, number) (((1 << num_of_bits) - 1) & (number >> index))



struct Frame
{
	uint64_t father_pm_address;
	uint64_t number;
};

struct Page
{
	Frame frame;
	uint64_t page_num;
};



uint64_t getOffset (uint64_t virtual_address, int depth)
{
	int idx, bits;
	idx = (TABLES_DEPTH - depth) * OFFSET_WIDTH;
	bits = MIN(VIRTUAL_ADDRESS_WIDTH - idx, OFFSET_WIDTH);
	return GET_BITS(idx, bits, virtual_address) % (PAGE_SIZE);
}

uint64_t getPhysicalAddress (uint64_t frame, uint64_t offset)
{
	return ((frame * PAGE_SIZE) + offset);
}



uint64_t getCyclicDistance (uint64_t page_swapped_in, uint64_t p)
{
	uint64_t diff = page_swapped_in-p;
	if(p > page_swapped_in){
		diff = p- page_swapped_in;
	}
	return MIN(NUM_PAGES - diff, diff);
}


void clearTable (uint64_t frameIndex)
{
	for (uint64_t i = 0; i < PAGE_SIZE; ++i)
	{
		PMwrite(getPhysicalAddress(frameIndex, i), 0);
	}
}



/**
 * DFS search algorithm for page tables tree
 * @param page - the page we want to map to the physical memory
 * @param source_frame - the frame we came from originally
 * @param empty_frame  - empty frame
 * @param evict_page - page to evict if pm memory is full
 * @param max_frame - maximal used frame number
 * @param max_dist - maximal cyclic distance
 * @param d - current depth
 * @param curr_frame  - current frame number
 * @param curr_vAddress - address to current node
 */
void DFS (uint64_t page, uint64_t source_frame, Frame & empty_frame, Page & evict_page, uint64_t & max_frame,
		  uint64_t & max_dist, int d, uint64_t father, uint64_t curr_frame, uint64_t curr_vAddress)
{
	// base case:
	if (d == TABLES_DEPTH)
	{
		uint64_t dist = getCyclicDistance(curr_vAddress, page);
		if (dist > max_dist and curr_frame != source_frame)
		{
			max_dist = dist;
			evict_page.frame.number = curr_frame;
			evict_page.frame.father_pm_address = father;
			evict_page.page_num = curr_vAddress;
		}
		return;
	}

	bool is_empty = true; // indicates the status of the current frame (empty_frame or not)
	uint64_t next_frame = 0, pm_address;
	for (uint64_t offset = 0; offset < PAGE_SIZE; offset++)
	{
		pm_address = getPhysicalAddress(curr_frame, offset);
		PMread(pm_address, (word_t *) &next_frame);
		if (next_frame != 0)
		{
			if (next_frame > max_frame)
			{
				max_frame = next_frame;
			}
			is_empty = false;
			DFS(page, source_frame, empty_frame, evict_page, max_frame, max_dist, d + 1, pm_address,
				next_frame, ((curr_vAddress << OFFSET_WIDTH) | offset));
		}
	}

	// if the current frame is empty_frame :
	// check that it is the first empty frame and that it is not our source frame
	if (is_empty and curr_frame != source_frame and empty_frame.number == 0)
	{
		empty_frame.number = curr_frame;
		empty_frame.father_pm_address = father;
	}

}


uint64_t getEmptyFrame (uint64_t page, uint64_t source_frame, int depth)
{
	uint64_t max_used_frame = 0, cyclic_dist = 0;
	Frame empty_frame = {0, 0};
	Page page_to_evict = {0, 0};
	DFS(page, source_frame, empty_frame, page_to_evict, max_used_frame, cyclic_dist, 0, 0, 0, 0);

	// ----  option 1: empty frame  (contains zeroes)  ----

	if (empty_frame.number != 0)
	{
		// go to frame father and write 0 instead
		PMwrite(empty_frame.father_pm_address, 0);
	}

	else
	{
		// ----  option 2: unused frame  ----

		if (max_used_frame < NUM_FRAMES - 1)
		{
			empty_frame.number = max_used_frame + 1;
		}

		// ----  option 3: memory is full - page eviction needed  ----

		else
		{
			if (page_to_evict.frame.number != 0)
			{
				// go to frame father and write 0 instead
				PMwrite(page_to_evict.frame.father_pm_address, 0);
				PMevict(page_to_evict.frame.number, page_to_evict.page_num );
				empty_frame.number = page_to_evict.frame.number;
			}
		}

	}
	if (depth < TABLES_DEPTH - 1) {
		// is not leaf
		clearTable(empty_frame.number);
	}
	return empty_frame.number;
}


void VMinitialize ()
{
	clearTable(0);
}



int ReadWrite(uint64_t virtualAddress, word_t *value, int action=READ){
	if (INVALID(virtualAddress, value))
	{
		return FAILURE;
	}
	uint64_t offset = 0, next_frame = 0, frame = 0, pm_address, page;
	page = virtualAddress >> OFFSET_WIDTH;
	for (uint64_t i = 0; i < TABLES_DEPTH; i++)
	{
		offset = getOffset(virtualAddress, i);
		pm_address = getPhysicalAddress(frame, offset);
		PMread(pm_address, (word_t *) (&next_frame));
		if (next_frame == 0)
		{ // need to find an empty frame
			next_frame = getEmptyFrame(page, frame, i);
			PMwrite(pm_address, next_frame); // link the new frame(node)

			if (i == TABLES_DEPTH - 1)
			{ // leaf
				PMrestore(next_frame, page);
			}
		}
		frame = next_frame;
	}
	offset = getOffset(virtualAddress, TABLES_DEPTH);
	pm_address = getPhysicalAddress(next_frame, offset);
	if(action == READ){
		PMread(pm_address, value);
	}
	else{
		PMwrite(pm_address, *value);
	}
	return SUCCESS;

}

int VMread (uint64_t virtualAddress, word_t *value)
{
	return ReadWrite(virtualAddress, value);
}


int VMwrite (uint64_t virtualAddress, word_t value)
{
	return ReadWrite(virtualAddress, &value, WRITE);
}