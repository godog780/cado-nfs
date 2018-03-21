#ifndef TREE_STATS_HPP_
#define TREE_STATS_HPP_

#include <string>
#include <vector>
#include <map>

class tree_stats {
    struct function_stats {
        unsigned int ncalled = 0;
        unsigned int min_inputsize = UINT_MAX;
        unsigned int max_inputsize = 0;
        unsigned int sum_inputsize = 0;
        unsigned int trimmed = 0;       /* either sum_inputsize or 0 */
        double spent = 0;
        double projected_time = 0;
        unsigned int projected_calls = 0;
        std::map<std::string, double> small_steps;
    };

    struct level_stats : public std::map<std::string, function_stats> {
        double projected_time(unsigned int, unsigned int);
        double last_printed_projected_time;
        unsigned int trimmed_here;       /* trimmed at this level ! */
    };


    struct running_stats {
        std::string func;
        unsigned int inputsize = 0;
        unsigned int trimmed = 0;
        double time_self = 0;
        double time_children = 0;
        std::map<std::string, double> small_steps;
        double * substep = NULL;
    };

    std::vector<level_stats> stack;
    std::vector<running_stats> curstack;

    unsigned int tree_total_breadth;
    double last_print_time;

    double begin;       /* stored as a wct_seconds() return value */


    void print(unsigned int level);

public:
    unsigned int depth;
    
    void enter(const char * func, unsigned int inputsize, unsigned int trimmed = 0); 
    void enter_norecurse(const char * func, unsigned int inputsize) { enter(func, inputsize, inputsize); }

    int leave(int rc);

    void begin_smallstep(const char * func MAYBE_UNUSED);
    void end_smallstep();
};

#endif	/* TREE_STATS_HPP_ */
