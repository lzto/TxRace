/*
 * explbr.cpp
 * Tool for lbr dump info explination
 * Jul 2015 Tong Zhang <ztong@vt.edu>
 */

#include <vector>
#include <set>
#include <iostream>
#include <fstream>
#include <algorithm>

#include <string.h>
#include <intel-lbr/lbr.h>

using namespace std;

bool opt_a = false;
bool opt_u = false;
char *opt_f = NULL;

void usage()
{
	cout << "./explbr [options]\n"
	     << "-f filename \n"
	     << "-a print all dump\n"
	     << "-u print all uniq dump\n";
	exit(1);
}

void dump(vector<lbr_stack>& lsv)
{
		for (std::vector<lbr_stack>::iterator it = lsv.begin();
		        it != lsv.end();
		        it++)
		{
			inteprete_lbr_info((lbr_stack*)&(*it));
			cout<<"--MARK--\n";
		}	
}

void work()
{
	vector<lbr_stack> lsv;
	ifstream lbr_dump_file;
	lbr_dump_file.open(opt_f, std::ifstream::in | std::ifstream::binary);
	lbr_stack temp;
	while (!lbr_dump_file.eof())
	{
		lbr_dump_file.read((char*)&temp, sizeof(lbr_stack));
		lsv.push_back(temp);
	}
	lbr_dump_file.close();
	cout << "Read " << lsv.size() << " entries\n";
	if (opt_a)
	{
		dump(lsv);
	} else if (opt_u)
	{
		vector<lbr_stack> lsv_uniq;
		for(std::vector<lbr_stack>::iterator it = lsv.begin();
			it!=lsv.end();
			it++)
		{
			if(std::find(lsv_uniq.begin(),lsv_uniq.end(),*it)==lsv_uniq.end())
			{
				lsv_uniq.push_back(*it);
			}
		}
		cout<<" Found "<<lsv_uniq.size()<<" uniq entries\n";
		dump(lsv_uniq);
	}
}

int main(int argc, char ** argv)
{
	if (argc < 2)
	{
		usage();
	}

	for (int i = 1; i < argc - 1; i++)
	{
		if (strcmp(argv[i], "-a") == 0)
		{
			opt_a = true;
		} else if (strcmp(argv[i], "-u") == 0)
		{
			opt_u = true;
		} else if (strcmp(argv[i], "-f") == 0)
		{
			opt_f = argv[i + 1];
		}
	}
	if(opt_f==NULL)
	{
		usage();
	}
	work();
}

bool operator==(const lbr_stack& one, const lbr_stack& another)
{
	if(memcmp(&one,&another,sizeof(lbr_stack))==0)
		return true;
	return false;
}

