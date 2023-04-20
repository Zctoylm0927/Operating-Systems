#include "mian.h"

#include <fstream>
#include <iostream>
#include <libpmemobj.h>
#include <map>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

#define MAX_BUF_LEN 10
using namespace std;

bool do_not_dump = false;

struct node {
	char key[key_length+1];
	char value[value_length+1];
};

struct my_root
{
    size_t len;
    node buf[MAX_BUF_LEN];
};

map<string, string> state;

static inline int file_exists(char const *file) { return access(file, F_OK); }

void exit_func()
{
    if (!do_not_dump)
    {
        FILE *fp = fopen("/ramdisk/qaq", "wb");

        int size = state.size();
        fwrite(&size, sizeof(size), 1, fp);

        for (auto &[k, v] : state)
        {
            fwrite(k.c_str(), key_length, 1, fp);
            fwrite(v.c_str(), value_length, 1, fp);
        }

        fclose(fp);
    }
}

void mian(std::vector<std::string> args)
{
//    atexit(&exit_func);

    auto filename = args[0].c_str();

    PMEMobjpool *pop;

    if (file_exists(filename) != 0)
    {
        pop = pmemobj_create(filename, "QAQ", 150L<<20, 0666);
    }
    else
    {
        pop = pmemobj_open(filename, "QAQ");
		
		PMEMoid root = pmemobj_root(pop, sizeof(struct my_root));
        struct my_root *rootp = (struct my_root *)pmemobj_direct(root);
        
        static int I = 0;
        for (; I < rootp->len; I++)
        {
        	char* k = rootp->buf[I].key;
        	char* v = rootp->buf[I].value;
            state[k] = v;
        } 

        do_not_dump = true;
    }

    if (pop == NULL)
    {
        std::cout << filename << std::endl;
        perror("pmemobj_create");
        return;
    }

    PMEMoid root = pmemobj_root(pop, sizeof(struct my_root));
    struct my_root *rootp = (struct my_root *)pmemobj_direct(root);

    char buf[MAX_BUF_LEN] = "114514";

    while (1)
    {
        Query q = nextQuery();

        switch (q.type)
        {

        case Query::SET:
        {
            node tmp;
            strcpy(tmp.key,q.key.c_str());
            strcpy(tmp.value,q.value.c_str());
            pmemobj_memcpy_persist(pop, rootp->buf + rootp->len, &tmp, sizeof(tmp));
   			rootp->len++;               
       	 	pmemobj_persist(pop, &rootp->len, sizeof(rootp->len));   
            break;
        }

        case Query::GET:
        {
            if (state.count(q.key))
                q.callback(state[q.key]);
            else
                q.callback("-");
            break;
        }

        case Query::NEXT:
        {
            if (auto it = state.upper_bound(q.key); it != state.end())
                q.callback(it->first);
            else
                q.callback("-");
            break;
        }

        default:
            throw std::invalid_argument(std::to_string(q.type));
        }
        
    }
    pmemobj_close(pop);
}
