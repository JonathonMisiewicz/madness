#ifndef WORLD_LOADBAL_DEUX
#define WORLD_LOADBAL_DEUX

#include <mra/mra.h>
#include <madness_config.h>
#include <map>

namespace madness {


    template <int NDIM>
    class LBDeuxPmap : public WorldDCPmapInterface< Key<NDIM> > {
        typedef Key<NDIM> keyT;
        typedef std::pair<keyT,ProcessID> pairT;
        typedef std::map<keyT,ProcessID> mapT;
        mapT map;
        typedef typename mapT::const_iterator iteratorT;
        
    public:
        LBDeuxPmap(const std::vector<pairT>& v) {
            for (unsigned int i=0; i<v.size(); i++) {
                map.insert(v[i]);
            }
        }

        ProcessID owner(const keyT& key) const {
            while (key.level() >= 0) {
                iteratorT it = map.find(key);
                if (it == map.end()) {
                    return owner(key.parent());
                }
                else {
                    return it->second;
                }
            }
            madness::print("Mon Dieux!", key);
            throw "LBDeuxPmap: lookup failed";
        }

        void print() const {
            madness::print("LBDeuxPmap");
        }
    };
    


    template <int NDIM>
    class LBNodeDeux {
        static const int nchild = (1<<NDIM);
        typedef Key<NDIM> keyT;
        typedef LBNodeDeux<NDIM> nodeT;
        typedef WorldContainer<keyT,nodeT> treeT;
        volatile double child_cost[nchild];
        volatile double my_cost;
        volatile double total_cost;
        volatile bool gotkids;
        volatile int nsummed;
        
        /// Computes index of child key in this node using last bit of translations
        int index(const keyT& key) {
            int ind = 0;
            for (int d=0; d<NDIM; d++) ind += ((key.translation()[d])&0x1) << d;
            return ind;
        }

    public:
        LBNodeDeux() 
            : my_cost(0.0), total_cost(0.0), gotkids(false), nsummed(0) {
            for (int i=0; i<nchild; i++) child_cost[i] = 0.0;
        }

        bool has_children() const {return gotkids;}

        double get_total_cost() const {return total_cost;}

        /// Accumulates cost into this node
        Void add(double cost, bool got_kids) {
            total_cost = (my_cost += cost);
            gotkids = gotkids || got_kids;
        }

        /// Accumulates cost up the tree from children
        Void sum(treeT* tree, const keyT& child, double value) {
            child_cost[index(child)] = value;
            nsummed++;
            if (nsummed == nchild) {
                for (int i=0; i<nchild; i++) total_cost += child_cost[i];
                if (child.level() > 1) {
                    keyT key = child.parent();
                    keyT parent = key.parent();
                    tree->send(parent, &nodeT::sum, tree, key, double(total_cost));
                }
            }
        }


        /// Logically deletes this node by setting cost to -1

        /// Cannot actually erase this node from the container since the send() handler
        /// is holding an accessor to it.
        Void deleter(treeT* tree, const keyT& key) {
            total_cost = my_cost = -1.0;
            if (has_children()) {
                for (KeyChildIterator<NDIM> kit(key); kit; ++kit) {
                    const keyT child = kit.key();
                    tree->send(child, &nodeT::deleter, tree, child);
                }
            }
        }
        

        /// Descends tree deleting all except internal nodes and sub-tree parents
        Void partition(treeT* tree, const keyT& key, double avg) {
            if (has_children()) {
                // Sort children in descending cost order
                keyT keys[nchild];
                double vals[nchild];
                for (KeyChildIterator<NDIM> kit(key); kit; ++kit) {
                    const keyT child = kit.key();
                    int ind = index(child);
                    keys[ind] = child;
                    vals[ind] = child_cost[ind];
                }
                for (int i=0; i<nchild; i++) {
                    for (int j=i+1; j<nchild; j++) {
                        if (vals[i] < vals[j]) {
                            std::swap(vals[i],vals[j]);
                            std::swap(keys[i],keys[j]);
                        }
                    }
                }
                
                // Split off subtrees in decreasing cost order
                for (int i=0; i<nchild; i++) {
                    if (total_cost <= avg) {
                        tree->send(keys[i], &nodeT::deleter, tree, keys[i]);
                    }
                    else {
                        total_cost -= vals[i];
                        tree->send(keys[i], &nodeT::partition, tree, keys[i], avg);
                    }
                }
            }
        }

        /// Printing for the curious
        Void print(treeT* tree, const keyT& key) const {
            for(int i=0; i<key.level(); i++) std::cout << "  ";
            madness::print(key, my_cost, total_cost);
            if (gotkids) {
                for (KeyChildIterator<NDIM> kit(key); kit; ++kit) {
                    tree->send(kit.key(), &nodeT::print, tree, kit.key());
                }
            }
        }

        template <typename Archive>
        void serialize(Archive& ar) {
            ar & archive::wrap_opaque(this,1);
        }
    };
        

    template <int NDIM>
    class LoadBalanceDeux {
        typedef Key<NDIM> keyT;
        typedef LBNodeDeux<NDIM> nodeT;
        typedef WorldContainer<keyT,nodeT> treeT;
        typedef typename treeT::iterator iteratorT;
        World& world;
        treeT tree;
        

        template <typename T, typename costT>
        struct add_op {
            LoadBalanceDeux* lb;
            const costT& costfn;
            add_op(LoadBalanceDeux* lb, const costT& costfn) : lb(lb), costfn(costfn) {}
            void operator()(const keyT& key, const FunctionNode<T,NDIM>& node) const {
                lb->tree.send(key, &nodeT::add, costfn(key,node), node.has_children());
            }
        };

        /// Sums costs up the tree returning to everyone the total cost
        double sum() {
            world.gop.fence();
            for (iteratorT it=tree.begin(); it!=tree.end(); ++it) {
                const keyT& key = it->first;
                const nodeT& node = it->second;
                if (!node.has_children() && key.level() > 0) {
                    tree.send(key.parent(), &nodeT::sum, &tree, key, node.get_total_cost());
                }
            }
            world.gop.fence();
            double total;
            keyT key0(0);
            if (world.rank() == tree.owner(key0)) {
                total = tree.find(key0).get()->second.get_total_cost();
            }
            world.gop.broadcast(total, tree.owner(key0));
            world.gop.fence();

            return total;
        }

        /// Used to sort results into descending order
        static bool compare(const std::pair<keyT,double>& a, const std::pair<keyT,double>& b) {
            return a.second < b.second;
        }


    public:
        LoadBalanceDeux(World& world) 
            : world(world)
            , tree(world, FunctionDefaults<NDIM>::get_pmap())
        {};

        /// Accumulates cost from a function
        template <typename T, typename costT>
        void add(const Function<T,NDIM>& f, const costT& costfn, bool fence=true) {
            const_cast<Function<T,NDIM>&>(f).unaryop_node(add_op<T,costT>(this,costfn), fence);
        }

        /// Printing for the curious
        void print_tree(const keyT& key = keyT(0)) {
            Future<iteratorT> futit = tree.find(key);
            iteratorT it = futit.get();
            if (it != tree.end()) {
                for(int i=0; i<key.level(); i++) std::cout << "  ";
                print(key, it->second.get_total_cost());
                
                if (it->second.has_children()) {
                    for (KeyChildIterator<NDIM> kit(key); kit; ++kit) {
                        print_tree(kit.key());
                    }
                }
            }
        }

        /// Actually does the partitioning of the tree
        SharedPtr< WorldDCPmapInterface<keyT> > partition() {
            // Compute full tree of costs
            double avg = sum()/world.size();
            avg = avg/4.0;
            if (world.rank() == 0) print_tree();
            world.gop.fence();
            
            // Create partitioning
            keyT key0(0);
            if (world.rank() == tree.owner(key0)) {
                tree.send(key0, &nodeT::partition, &tree, key0, avg*1.1);
            }
            world.gop.fence();

            // Collect entire vector onto node0
            vector< std::pair<keyT,double> > results;
            for (iteratorT it=tree.begin(); it!=tree.end(); ++it) {
                if (it->second.get_total_cost() > 0) {
                    results.push_back(std::make_pair(it->first,it->second.get_total_cost()));
                }
            }
            results = world.gop.concat0(results);

            vector< std::pair<keyT,ProcessID> > map;

            if (world.rank() == 0) {

                std::sort(results.begin(), results.end(), compare);
                
                // Now we stupidly just map the sorted keys to processors.
                // Lots of room for more intelligence here.

                map.reserve(results.size());

                vector<double> costs(world.size(), 0.0);
                ProcessID p=0;
                int inc=1;
                while (results.size()) {
                    const std::pair<keyT,double>& f = results.back();
                    ProcessID proc;
                    if (f.first.level() == 0) {
                        proc = 0;
                    }
                    else {
                        proc = p;
                        p += inc;
                        if (p < 0) {
                            p++; inc=1;
                        }
                        else if (p >= world.size()) {
                            p--; inc=-1;
                        }
                    }
                    costs[proc] += f.second;
                    map.push_back(std::make_pair(f.first,proc));
                    results.pop_back();
                }
                print("THIS IS THE MAP");
                print(map);
                print("THIS IS THE COSTS");
                print(costs);
            }

            world.gop.broadcast_serializable(map, 0);
            
            // Return the Procmap

            return SharedPtr< WorldDCPmapInterface<keyT> >(new LBDeuxPmap<NDIM>(map));
        }
    };
}


#endif

