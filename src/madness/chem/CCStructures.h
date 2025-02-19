/*
 * CCStructures.h
 *
 *  Created on: Sep 3, 2015
 *      Author: kottmanj
 */


/// File holds all helper structures necessary for the CC_Operator and CC2 class
#ifndef CCSTRUCTURES_H_
#define CCSTRUCTURES_H_

#include <madness/mra/mra.h>
#include<madness/mra/commandlineparser.h>
#include<madness/chem/ccpairfunction.h>
#include<madness/mra/QCCalculationParametersBase.h>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <madness/mra/macrotaskq.h>

namespace madness {

/// Operatortypes used by the CCConvolutionOperator Class
enum OpType {
    OT_UNDEFINED,
    OT_ONE,         /// indicates the identity
    OT_G12,         /// 1/r
    OT_SLATER,      /// exp(r)
    OT_F12,         /// 1-exp(r)
    OT_FG12,        /// (1-exp(r))/r
    OT_F212,        /// (1-exp(r))^2
    OT_BSH          /// exp(r)/r
};

/// Calculation Types used by CC2
enum CalcType {
    CT_UNDEFINED, CT_MP2, CT_CC2, CT_LRCCS, CT_LRCC2, CT_CISPD, CT_ADC2, CT_TDHF, CT_TEST
};
/// Type of Pairs used by CC_Pair2 class
enum CCState {
    CCSTATE_UNDEFINED, GROUND_STATE, EXCITED_STATE
};
/// CC2 Singles Potentials
enum PotentialType {
    POT_UNDEFINED,
    POT_F3D_,
    POT_s3a_,
    POT_s3b_,
    POT_s3c_,
    POT_s5a_,
    POT_s5b_,
    POT_s5c_,
    POT_s2b_,
    POT_s2c_,
    POT_s4a_,
    POT_s4b_,
    POT_s4c_,
    POT_s6_,
    POT_ccs_,
    POT_cis_,
    POT_singles_
};

/// Assigns strings to enums for formated output
std::string
assign_name(const PairFormat& input);

/// Assigns strings to enums for formated output
std::string
assign_name(const CCState& input);

/// Assigns strings to enums for formated output
std::string
assign_name(const OpType& input);

/// Assigns enum to string
CalcType
assign_calctype(const std::string name);

/// Assigns strings to enums for formated output
std::string
assign_name(const CalcType& inp);

/// Assigns strings to enums for formated output
std::string
assign_name(const PotentialType& inp);

/// Assigns strings to enums for formated output
std::string
assign_name(const FuncType& inp);

// Little structure for formated output and to collect warnings
// much room to improve
struct CCMessenger {
    CCMessenger(World& world) : world(world), output_prec(10), scientific(true), debug(false), os(std::cout) {}

    World& world;
    size_t output_prec;
    bool scientific;
    bool debug;

    void operator()(const std::string& msg) const { output(msg); }

    void debug_output(const std::string& msg) const {
        if (debug) output(msg);
    }

    void
    output(const std::string& msg) const;

    void
    section(const std::string& msg) const;

    void
    subsection(const std::string& msg) const;

    void
    warning(const std::string& msg) const;

    void print_warnings() const {
        for (const auto& x:warnings) if (world.rank() == 0) std::cout << x << "\n";
    }

    template<class T>
    CCMessenger operator<<(const T& t) const {
        using madness::operators::operator<<;
        if (world.rank() == 0) os << t;
        return *this;
    }

    /// collect all warnings that occur to print out at the end of the job
    mutable std::vector<std::string> warnings;
    /// output stream
    std::ostream& os;
};


/// Timer Structure
struct CCTimer {
    /// TDA_TIMER constructor
    /// @param[in] world the world
    /// @param[in] msg	a string that contains the desired printout when info function is called
    CCTimer(World& world, std::string msg) : world(world), start_wall(wall_time()), start_cpu(cpu_time()),
                                             operation(msg), end_wall(0.0), end_cpu(0.0), time_wall(-1.0),
                                             time_cpu(-1.0) {}

    World& world;
    double start_wall;
    double start_cpu;
    std::string operation;
    double end_wall;
    double end_cpu;
    double time_wall;
    double time_cpu;

    void update_time() {
        time_wall = wall_time() - start_wall;
        time_cpu = cpu_time() - start_cpu;
    }

public:
    /// print out information about the passed time since the CC_TIMER object was created
    void
    info(const bool debug = true, const double norm = 12345.6789);

    CCTimer start() {
        start_wall = wall_time();
        start_cpu = cpu_time();
        return *this;
    }

    CCTimer stop() {
        end_wall = wall_time();
        end_cpu = cpu_time();
        time_wall = end_wall - start_wall;
        time_cpu = end_cpu - start_cpu;
        return *this;
    }

    double reset() {
        stop();
        double wtime=time_wall;
        start();
        return wtime;
    }


    double get_wall_time_diff() const { return end_wall; }

    double get_cpu_time_diff() const { return end_cpu; }

    std::pair<double, double> current_time(bool printout = false) {
        if (time_wall < 0.0 or time_cpu < 0.0) stop();
        return std::make_pair(time_wall, time_cpu);
    }

    void print() {
        print(current_time());
    }

    void print() const {
        print(std::make_pair(time_wall, time_cpu));
    }

    void print(const std::pair<double, double>& times) const {
        if (world.rank() == 0) {
            std::cout << std::setfill(' ') << std::scientific << std::setprecision(2)
                      << "Timer: " << times.first << " (Wall), " << times.second << " (CPU)" << ", (" + operation + ")"
                      << "\n";
        }
    }
};

/// Calculation TDHFParameters for CC2 and TDA calculations
/// Maybe merge this with calculation_parameters of SCF at some point, or split into TDA and CC
struct CCParameters : public QCCalculationParametersBase {

    CCParameters() {
        initialize_parameters();
    };

    /// copy constructor
    CCParameters(const CCParameters& other) =default;

    /// ctor reading out the input file
    CCParameters(World& world, const commandlineparser& parser) {
        initialize_parameters();
        read_input_and_commandline_options(world,parser,"cc2");
        set_derived_values();
    };

    void initialize_parameters() {
        double thresh=1.e-3;
        double thresh_operators=1.e-6;
        initialize < std::string > ("calc_type", "mp2", "the calculation type", {"mp2", "cc2", "cis", "lrcc2", "cispd", "adc2", "test"});
        initialize < double > ("lo", 1.e-7, "the finest length scale to be resolved by 6D operators");
        initialize < double > ("dmin", 1.0, "defines the depth of the special level");
        initialize < double > ("thresh_6d", thresh, "threshold for the 6D wave function");
        initialize < double > ("tight_thresh_6d", 0.1*thresh, "tight threshold for the 6D wave function");
        initialize < double > ("thresh_3d", 0.01*thresh, "threshold for the 3D reference wave function");
        initialize < double > ("tight_thresh_3d", 0.001*thresh, "tight threshold for the 3D reference wave function");
        initialize < double > ("thresh_bsh_3d", thresh_operators, "threshold for BSH operators");
        initialize < double > ("thresh_bsh_6d", thresh_operators, "threshold for BSH operators");
        initialize < double > ("thresh_poisson", thresh_operators, "threshold for Poisson operators");
        initialize < double > ("thresh_f12", thresh_operators, "threshold for Poisson operators");
        initialize < double > ("thresh_Ue", thresh_operators, "ue threshold");
        initialize < double > ("econv", thresh, "overal convergence threshold ");
        initialize < double > ("econv_pairs", 0.1*thresh, "convergence threshold for pairs");
        initialize < double > ("dconv_3d", 0.01*thresh, "convergence for cc singles");
        initialize < double > ("dconv_6d", thresh, "convergence for cc doubles");
        initialize < std::size_t > ("iter_max", 10, "max iterations");
        initialize < std::size_t > ("iter_max_3d", 10, "max iterations");
        initialize < std::size_t > ("iter_max_6d", 10, "max iterations");
        initialize < std::pair<int, int>> ("only_pair", {-1, -1}, "compute only a single pair");
        initialize < bool > ("restart", false, "restart");
        initialize < bool > ("no_compute", false, "no compute");
        initialize < bool > ("no_compute_gs", false, "no compute");
        initialize < bool > ("no_compute_mp2_constantpart", false, "no compute");
        initialize < bool > ("no_compute_response", false, "no compute");
        initialize < bool > ("no_compute_mp2", false, "no compute");
        initialize < bool > ("no_compute_cc2", false, "no compute");
        initialize < bool > ("no_compute_cispd", false, "no compute");
        initialize < bool > ("no_compute_lrcc2", false, "no compute");
        initialize < double > ("corrfac_gamma", 1.0, "exponent for the correlation factor");
        initialize < std::size_t > ("output_prec", 8, "for formatted output");
        initialize < bool > ("debug", false, "");
        initialize < bool > ("plot", false, "");
        initialize < bool > ("kain", true, "");
        initialize < std::size_t > ("kain_subspace", 3, "");
        initialize < long > ("freeze", -1, "number of frozen orbitals: -1: automatic");
        initialize < bool > ("test", false, "");
        // choose if Q for the constant part of MP2 and related calculations should be decomposed: GQV or GV - GO12V
        initialize < bool > ("decompose_Q", true, "");
        // if true the ansatz for the CC2 ground state pairs is |tau_ij> = |u_ij> + Qtf12|titj>, with Qt = Q - |tau><phi|
        // if false the ansatz is the same with normal Q projector
        // the response ansatz is the corresponding response of the gs ansatz
        initialize < bool > ("QtAnsatz", true, "");
        // a vector containing the excitations which shall be optizmized later (with CIS(D) or CC2)
        initialize < std::vector<size_t>>
        ("excitations", {}, "vector containing the excitations");
    }

    void set_derived_values();

    CalcType calc_type() const {
        std::string value = get<std::string>("calc_type");
        if (value == "mp2") return CT_MP2;
        if (value == "cc2") return CT_CC2;
        if (value == "cis") return CT_LRCCS;
        if (value == "lrcc2") return CT_LRCC2;
        if (value == "cispd") return CT_CISPD;
        if (value == "adc2") return CT_ADC2;
        if (value == "test") return CT_TEST;
        MADNESS_EXCEPTION("faulty CalcType", 1);
    }

    bool response() const {return calc_type()==CT_ADC2 or calc_type()==CT_CISPD or calc_type()==CT_LRCC2 or calc_type()==CT_LRCCS;}
    double lo() const { return get<double>("lo"); }

    double dmin() const { return get<double>("dmin"); }

    double thresh_3D() const { return get<double>("thresh_3d"); }

    double tight_thresh_3D() const { return get<double>("tight_thresh_3d"); }

    double thresh_6D() const { return get<double>("thresh_6d"); }

    double tight_thresh_6D() const { return get<double>("tight_thresh_6d"); }

    double thresh_bsh_3D() const { return get<double>("thresh_bsh_3d"); }

    double thresh_bsh_6D() const { return get<double>("thresh_bsh_6d"); }

    double thresh_poisson() const { return get<double>("thresh_poisson"); }

    double thresh_f12() const { return get<double>("thresh_f12"); }

    double thresh_Ue() const { return get<double>("thresh_ue"); }

    double econv() const { return get<double>("econv"); }

    double econv_pairs() const { return get<double>("econv_pairs"); }

    double dconv_3D() const { return get<double>("dconv_3d"); }

    double dconv_6D() const { return get<double>("dconv_6d"); }

    std::size_t iter_max() const { return get<std::size_t>("iter_max"); }

    std::size_t iter_max_3D() const { return get<std::size_t>("iter_max_3d"); }

    std::size_t iter_max_6D() const { return get<std::size_t>("iter_max_6d"); }

    std::pair<int, int> only_pair() const { return get<std::pair<int, int>>("only_pair"); }

    bool restart() const { return get<bool>("restart"); }

    bool no_compute() const { return get<bool>("no_compute"); }

    bool no_compute_gs() const { return get<bool>("no_compute_gs"); }

    bool no_compute_mp2_constantpart() const { return get<bool>("no_compute_mp2_constantpart"); }

    bool no_compute_response() const { return get<bool>("no_compute_response"); }

    bool no_compute_mp2() const { return get<bool>("no_compute_mp2"); }

    bool no_compute_cc2() const { return get<bool>("no_compute_cc2"); }

    bool no_compute_cispd() const { return get<bool>("no_compute_cispd"); }

    bool no_compute_lrcc2() const { return get<bool>("no_compute_lrcc2"); }

    bool debug() const { return get<bool>("debug"); }

    bool plot() const { return get<bool>("plot"); }

    bool kain() const { return get<bool>("kain"); }

    bool test() const { return get<bool>("test"); }

    bool decompose_Q() const { return get<bool>("decompose_q"); }

    bool QtAnsatz() const { return get<bool>("qtansatz"); }

    std::size_t output_prec() const { return get<std::size_t>("output_prec"); }

    std::size_t kain_subspace() const { return get<std::size_t>("kain_subspace"); }

    long freeze() const { return get<long>("freeze"); }

    std::vector<std::size_t> excitations() const { return get<std::vector<std::size_t>>("excitations"); }

    double gamma() const {return get<double>("corrfac_gamma");}

    /// print out the parameters
    void information(World& world) const;

    /// check if parameters are set correct
    void sanity_check(World& world) const;

    void error(World& world, const std::string& msg) const {
        if (world.rank() == 0)
            std::cout << "\n\n\n\n\n!!!!!!!!!\n\nERROR IN CC_PARAMETERS:\n    ERROR MESSAGE IS: " << msg
                      << "\n\n\n!!!!!!!!" << std::endl;
        MADNESS_EXCEPTION("ERROR IN CC_PARAMETERS", 1);
    }

    size_t warning(World& world, const std::string& msg) const {
        if (world.rank() == 0) std::cout << "WARNING IN CC_PARAMETERS!: " << msg << std::endl;
        return 1;
    }

};

struct PairVectorMap {

    std::vector<std::pair<int, int>> map; ///< maps pair index (i,j) to vector index k
    PairVectorMap(const std::vector<std::pair<int, int>> map1) : map(map1) {}

    static PairVectorMap triangular_map(const int nfreeze, const int nocc) {
        std::vector<std::pair<int, int>> map; ///< maps pair index (i,j) to vector index k
        for (int i=nfreeze; i<nocc; ++i) {
            for (int j=i; j<nocc; ++j) {
                map.push_back(std::make_pair(i,j));
            }
        }
        return PairVectorMap(map);
    }

    static PairVectorMap quadratic_map(const int nfreeze, const int nocc) {
        std::vector<std::pair<int, int>> map; ///< maps pair index (i,j) to vector index k
        for (int i=nfreeze; i<nocc; ++i) {
            for (int j=nfreeze; j<nocc; ++j) {
                map.push_back(std::make_pair(i,j));
            }
        }
        return PairVectorMap(map);
    }

    void print(const std::string msg="PairVectorMap") const {
        madness::print(msg);
        madness::print("vector element <-> pair index");
        for (int i=0; i<map.size(); ++i) {
            madness::print(i, " <-> ",map[i]);
        }
    }

};

/// POD holding all electron pairs with easy access
/// Similar strucutre than the Pair structure from MP2 but with some additional features (merge at some point)
/// This structure will also be used for intermediates
template<typename T>
struct Pairs {

    typedef std::map<std::pair<int, int>, T> pairmapT;
    pairmapT allpairs;

    /// convert Pairs<T> to another type

    /// opT op takes an object of T and returns the result type
    template<typename R, typename opT>
    Pairs<R> convert(const Pairs<T> arg, const opT op) const {
        Pairs<R> result;
        for (auto& p : arg.allpairs) {
            int i=p.first.first;
            int j=p.first.second;
            result.insert(i,j,op(p.second));
        }
        return result;
    }

    static Pairs vector2pairs(const std::vector<T>& argument, const PairVectorMap map) {
        Pairs<T> pairs;
        for (int i=0; i<argument.size(); ++i) {
            pairs.insert(map.map[i].first,map.map[i].second,argument[i]);
        }
        return pairs;
    }

    static std::vector<T> pairs2vector(const Pairs<T>& argument, const PairVectorMap map) {
        std::vector<T> vector;
        for (int i=0; i<argument.allpairs.size(); ++i) {
            vector.push_back(argument(map.map[i].first,map.map[i].second));
        }
        return vector;
    }

    /// getter
    const T& operator()(int i, int j) const {
        return allpairs.at(std::make_pair(i, j));
    }

    /// getter
    // at instead of [] operator bc [] inserts new element if nothing is found while at throws out of range error
    T& operator()(int i, int j) {
        return allpairs.at(std::make_pair(i, j));
    }

    /// setter
    /// can NOT replace elements (for this construct new pair map and swap the content)
    void insert(int i, int j, const T& pair) {
        std::pair<int, int> key = std::make_pair(i, j);
        allpairs.insert(std::make_pair(key, pair));
    }

    /// swap the contant of the pairmap
    void swap(Pairs<T>& other) {
        allpairs.swap(other.allpairs);
    }

    bool empty() const {
        if (allpairs.size() == 0) return true;
        else return false;
    }
};

/// f12 and g12 intermediates of the form <f1|op|f2> (with op=f12 or op=g12) will be saved using the pair structure
typedef Pairs<real_function_3d> intermediateT;

/// Returns the size of an intermediate
double
size_of(const intermediateT& im);



// structure for CC Vectorfunction
/// A helper structure which holds a map of functions
struct CC_vecfunction : public archive::ParallelSerializableObject {

    CC_vecfunction() : type(UNDEFINED), omega(0.0), current_error(99.9), delta(0.0) {}

    CC_vecfunction(const FuncType type_) : type(type_), omega(0.0), current_error(99.9), delta(0.0) {}

    CC_vecfunction(const vector_real_function_3d& v) : type(UNDEFINED), omega(0.0), current_error(99.9), delta(0.0) {
        for (size_t i = 0; i < v.size(); i++) {
            CCFunction tmp(v[i], i, type);
            functions.insert(std::make_pair(i, tmp));
        }
    }

    CC_vecfunction(const std::vector<CCFunction>& v) : type(UNDEFINED), omega(0.0), current_error(99.9), delta(0.0) {
        for (size_t i = 0; i < v.size(); i++) {
            functions.insert(std::make_pair(v[i].i, v[i]));
        }
    }

    CC_vecfunction(const vector_real_function_3d& v, const FuncType& type) : type(type), omega(0.0),
                                                                             current_error(99.9), delta(0.0) {
        for (size_t i = 0; i < v.size(); i++) {
            CCFunction tmp(v[i], i, type);
            functions.insert(std::make_pair(i, tmp));
        }
    }

    CC_vecfunction(const vector_real_function_3d& v, const FuncType& type, const size_t& freeze) : type(type),
                                                                                                   omega(0.0),
                                                                                                   current_error(99.9),
                                                                                                   delta(0.0) {
        for (size_t i = 0; i < v.size(); i++) {
            CCFunction tmp(v[i], freeze + i, type);
            functions.insert(std::make_pair(freeze + i, tmp));
        }
    }

    CC_vecfunction(const std::vector<CCFunction>& v, const FuncType type_)
            : type(type_), omega(0.0), current_error(99.9), delta(0.0) {
        for (auto x:v) functions.insert(std::make_pair(x.i, x));
    }

    /// copy ctor (shallow)
    CC_vecfunction(const CC_vecfunction& other)
            : functions(other.functions), type(other.type), omega(other.omega),
              current_error(other.current_error),
              delta(other.delta), irrep(other.irrep) {
    }

    /// assignment operator
//    CC_vecfunction& operator=(const CC_vecfunction& other) = default;
    CC_vecfunction& operator=(const CC_vecfunction& other) {
        if (this == &other) return *this;
        functions = other.functions;
        type = other.type;
        omega = other.omega;
        current_error = other.current_error;
        delta = other.delta;
        irrep = other.irrep;
        return *this;
    }


    /// returns a deep copy (void shallow copy errors)
    CC_vecfunction
    copy() const;

    static CC_vecfunction load_restartdata(World& world, std::string filename) {
        archive::ParallelInputArchive<archive::BinaryFstreamInputArchive> ar(world, filename.c_str());
        CC_vecfunction tmp;
        ar & tmp;
        return tmp;
    }

    void save_restartdata(World& world, std::string filename) const {
        archive::ParallelOutputArchive<archive::BinaryFstreamOutputArchive> ar(world, filename.c_str());
        ar & *this;
    }

    template<typename Archive>
    void serialize(const Archive& ar) {
        typedef std::vector<std::pair<std::size_t, CCFunction>> CC_functionvec;

        auto map2vector = [] (const CC_functionmap& map) {
            return CC_functionvec(map.begin(), map.end());
        };
        auto vector2map = [] (const CC_functionvec& vec) {
            return CC_functionmap(vec.begin(), vec.end());
        };

        ar & type & omega & current_error & delta & irrep ;
        if (ar.is_input_archive) {
	    std::size_t size=0; // set to zero to silence compiler warning
            ar & size;
            CC_functionvec tmp(size);

            for (auto& t : tmp) ar & t.first & t.second;
            functions=vector2map(tmp);
        } else if (ar.is_output_archive) {
            auto tmp=map2vector(functions);
            ar & tmp.size();
            for (auto& t : tmp) ar & t.first & t.second;
        }
    }

    typedef std::map<std::size_t, CCFunction> CC_functionmap;
    CC_functionmap functions;

    FuncType type;
    double omega; /// excitation energy
    double current_error;
    double delta; // Last difference in Energy
    std::string irrep = "null";    /// excitation irrep (direct product of x function and corresponding orbital)

    std::string
    name(const int ex) const;

    bool is_converged(const double econv, const double dconv) const {
        return (current_error<dconv) and (std::fabs(delta)<econv);
    }

    /// getter
    const CCFunction& operator()(const CCFunction& i) const {
        return functions.find(i.i)->second;
    }

    /// getter
    const CCFunction& operator()(const size_t& i) const {
        return functions.find(i)->second;
    }

    /// getter
    CCFunction& operator()(const CCFunction& i) {
        return functions[i.i];
    }

    /// getter
    CCFunction& operator()(const size_t& i) {
        return functions[i];
    }

    /// setter
    void insert(const size_t& i, const CCFunction& f) {
        functions.insert(std::make_pair(i, f));
    }

    /// setter
    void set_functions(const vector_real_function_3d& v, const FuncType& type, const size_t& freeze) {
        functions.clear();
        for (size_t i = 0; i < v.size(); i++) {
            CCFunction tmp(v[i], freeze + i, type);
            functions.insert(std::make_pair(freeze + i, tmp));
        }
    }

    /// Returns all the functions of the map as vector
    vector_real_function_3d get_vecfunction() const {
        vector_real_function_3d tmp;
        for (auto x:functions) tmp.push_back(x.second.function);
        return tmp;
    }

    /// Get the size vector (number of functions in the map)
    size_t size() const {
        return functions.size();
    }

    /// Print the memory of which is used by all the functions in the map
    void
    print_size(const std::string& msg = "!?not assigned!?") const;

    /// scalar multiplication
    CC_vecfunction operator*(const double& fac) const {
        vector_real_function_3d vnew = fac * get_vecfunction();
        const size_t freeze = functions.cbegin()->first;
        return CC_vecfunction(vnew, type, freeze);
    }

    /// scaling (inplace)
    void scale(const double& factor) {
        for (auto& ktmp:functions) {
            ktmp.second.function.scale(factor);
        }
    }

    /// operator needed for sort operation (sorted by omega values)
    bool operator<(const CC_vecfunction& b) const { return omega < b.omega; }

    // plotting
    void plot(const std::string& msg = "") const {
        for (auto& ktmp:functions) {
            ktmp.second.plot(msg);
        }
    }
public:
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CC_vecfunction, omega, irrep, current_error)

};

/// Helper Structure that carries out operations on CC_functions
/// The structure can hold intermediates for g12 and f12 of type : <mo_bra_k|op|type> with type=HOLE,PARTICLE or RESPONSE
/// some 6D operations are also included
/// The structure does not know if nuclear correlation facors are used, so the corresponding bra states have to be prepared beforehand
struct CCConvolutionOperator {

    /// parameter class
    struct Parameters {
        Parameters() {};

        Parameters(const Parameters& other) :
                thresh_op(other.thresh_op),
                lo(other.lo),
                freeze(other.freeze),
                gamma(other.gamma) {
        }

        Parameters(const CCParameters& param) : thresh_op(param.thresh_poisson()), lo(param.lo()),
                                                freeze(param.freeze()),
                                                gamma(param.gamma()) {};
        double thresh_op = FunctionDefaults<3>::get_thresh();
        double lo = 1.e-6;
        int freeze = 0;
        double gamma = 1.0; /// f12 exponent
    };


    /// @param[in] world
    /// @param[in] optype: the operatortype (can be g12_ or f12_)
    /// @param[in] param: the parameters of the current CC-Calculation (including function and operator thresholds and the exponent for f12)
    CCConvolutionOperator(World& world, const OpType type, Parameters param) : parameters(param), world(world),
                                                                               operator_type(type),
                                                                               op(init_op(operator_type, parameters)) {
    }

    CCConvolutionOperator(const CCConvolutionOperator& other) = default;

    friend bool can_combine(const CCConvolutionOperator& left, const CCConvolutionOperator& right) {
        return (combine_OT(left,right).first!=OT_UNDEFINED);
    }

    friend std::pair<OpType,Parameters> combine_OT(const CCConvolutionOperator& left, const CCConvolutionOperator& right) {
        OpType type=OT_UNDEFINED;
        Parameters param=left.parameters;
        if ((left.type()==OT_F12) and (right.type()==OT_G12)) {
            type=OT_FG12;
        }
        if ((left.type()==OT_G12) and (right.type()==OT_F12)) {
            type=OT_FG12;
            param.gamma=right.parameters.gamma;
        }
        if ((left.type()==OT_F12) and (right.type()==OT_F12)) {
            type=OT_F212;
            // keep the original gamma
            // (f12)^2 = (1- slater12)^2  = 1/(4 gamma) (1 - 2 exp(-gamma) + exp(-2 gamma))
            MADNESS_CHECK(right.parameters.gamma == left.parameters.gamma);
        }
        return std::make_pair(type,param);
    }


    /// combine 2 convolution operators to one

    /// @return a vector of pairs: factor and convolution operator
    friend std::vector<std::pair<double,CCConvolutionOperator>> combine(const CCConvolutionOperator& left, const CCConvolutionOperator& right) {
        MADNESS_CHECK(can_combine(left,right));
        MADNESS_CHECK(left.world.id()==right.world.id());
        auto [type,param]=combine_OT(left,right);
        std::vector<std::pair<double,CCConvolutionOperator>> result;
        if (type==OT_FG12) {
            // fg = (1 - exp(-gamma r12))  / r12 = 1/r12 - exp(-gamma r12)/r12 = coulomb - bsh

            // coulombfit return 1/r
            // we need 1/(2 gamma) 1/r
            result.push_back(std::make_pair(1.0/(2.0*param.gamma),CCConvolutionOperator(left.world, OT_G12, param)));

            // bshfit returns 1/(4 pi) exp(-gamma r)/r
            // we need 1/(2 gamma) exp(-gamma r)/r
            const double factor = 4.0 * constants::pi /(2.0*param.gamma);
            result.push_back(std::make_pair(-factor,CCConvolutionOperator(left.world, OT_BSH, param)));
        } else if (type==OT_F212) {
//             we use the slater operator which is S = e^(-y*r12), y=gamma
//             the f12 operator is: 1/2y*(1-e^(-y*r12)) = 1/2y*(1-S)
//             so the squared f12 operator is: f*f = 1/(4*y*y)(1-2S+S*S), S*S = S(2y) = e(-2y*r12)
//             we have then: <xy|f*f|xy> = 1/(4*y*y)*(<xy|xy> - 2*<xy|S|xy> + <xy|SS|xy>)
//             we have then: <xy|f*f|xy> =(<xy|f12|xy> -  1/(4*y*y)*2*<xy|S|xy>
            MADNESS_CHECK(left.parameters.gamma==right.parameters.gamma);
            const double prefactor = 1.0 / (4.0 * param.gamma); // Slater has no 1/(2 gamma) per se.
            Parameters param2=param;
            param2.gamma*=2.0;
            result.push_back(std::make_pair(1.0*prefactor,CCConvolutionOperator(left.world, OT_ONE, param)));
            result.push_back(std::make_pair(-2.0*prefactor,CCConvolutionOperator(left.world, OT_SLATER, left.parameters)));
            result.push_back(std::make_pair(1.0*prefactor,CCConvolutionOperator(left.world, OT_SLATER, param2)));
        }
        return result;
    }

    /// @param[in] f: a 3D function
    /// @param[out] the convolution op(f), no intermediates are used
    real_function_3d operator()(const real_function_3d& f) const {
        if (op) return ((*op)(f)).truncate();
        return f;
    }

    /// @param[in] bra a CC_vecfunction
    /// @param[in] ket a CC_function
    /// @param[out] vector[i] = <bra[i]|op|ket>
    vector_real_function_3d operator()(const CC_vecfunction& bra, const CCFunction& ket) const {
        MADNESS_CHECK(op);
        vector_real_function_3d result;
        if (bra.type == HOLE) {
            for (const auto& ktmp:bra.functions) {
                const CCFunction& brai = ktmp.second;
                const real_function_3d tmpi = this->operator()(brai, ket);
                result.push_back(tmpi);
            }
        } else {
            vector_real_function_3d tmp = mul(world, ket.function, bra.get_vecfunction());
            result = apply(world, (*op), tmp);
            truncate(world, result);
        }
        return result;
    }

    // @param[in] f: a vector of 3D functions
    // @param[out] the convolution of op with each function, no intermeditates are used
    vector_real_function_3d operator()(const vector_real_function_3d& f) const {
        if (op) return apply<double, double, 3>(world, (*op), f);
        return f;
    }

    // @param[in] bra: a 3D CC_function, if nuclear-correlation factors are used they have to be applied before
    // @param[in] ket: a 3D CC_function,
    // @param[in] use_im: default is true, if false then no intermediates are used
    // @param[out] the convolution <bra|op|ket> = op(bra*ket), if intermediates were calculated before the operator uses them
    real_function_3d operator()(const CCFunction& bra, const CCFunction& ket, const bool use_im = true) const;

    // @param[in] u: a 6D-function
    // @param[out] the convolution \int g(r,r') u(r,r') dr' (if particle==2) and g(r,r') u(r',r) dr' (if particle==1)
    // @param[in] particle: specifies on which particle of u the operator will act (particle ==1 or particle==2)
    real_function_6d operator()(const real_function_6d& u, const size_t particle) const;

    // @param[in] bra: a 3D-CC_function, if nuclear-correlation factors are used they have to be applied before
    // @param[in] u: a 6D-function
    // @param[in] particle: specifies on which particle of u the operator will act (particle ==1 or particle==2)
    // @param[out] the convolution <bra|g12|u>_particle
    real_function_3d operator()(const CCFunction& bra, const real_function_6d& u, const size_t particle) const;

    /// @param[in] bra: a vector of CC_functions, the type has to be HOLE
    /// @param[in] ket: a vector of CC_functions, the type can be HOLE,PARTICLE,RESPONSE
    /// updates intermediates of the type <bra|op|ket>
    void update_elements(const CC_vecfunction& bra, const CC_vecfunction& ket);

    /// @param[out] prints the name of the operator (convenience) which is g12 or f12 or maybe other things like gf in the future
    std::string name() const { return assign_name(operator_type); }

    /// @param[in] the type of which intermediates will be deleted
    /// e.g if(type==HOLE) then all intermediates of type <mo_bra_k|op|HOLE> will be deleted
    void clear_intermediates(const FuncType& type);

    /// prints out information (operatorname, number of stored intermediates ...)
    size_t info() const;

    /// sanity check .. doens not do so much
    void sanity() const { print_intermediate(HOLE); }

    /// @param[in] type: the type of intermediates which will be printed, can be HOLE,PARTICLE or RESPONSE
    void print_intermediate(const FuncType type) const {
        if (type == HOLE)
            for (const auto& tmp:imH.allpairs)
                tmp.second.print_size("<H" + std::to_string(tmp.first.first) + "|" + assign_name(operator_type) + "|H" +
                                      std::to_string(tmp.first.second) + "> intermediate");
        else if (type == PARTICLE)
            for (const auto& tmp:imP.allpairs)
                tmp.second.print_size("<H" + std::to_string(tmp.first.first) + "|" + assign_name(operator_type) + "|P" +
                                      std::to_string(tmp.first.second) + "> intermediate");
        else if (type == RESPONSE)
            for (const auto& tmp:imR.allpairs)
                tmp.second.print_size("<H" + std::to_string(tmp.first.first) + "|" + assign_name(operator_type) + "|R" +
                                      std::to_string(tmp.first.second) + "> intermediate");
    }

    /// create a TwoElectronFactory with the operatorkernel
    TwoElectronFactory get_kernel() const {
        if (type() == OT_G12) return TwoElectronFactory(world).dcut(1.e-7);
        else if (type() == OT_F12) return TwoElectronFactory(world).dcut(1.e-7).f12().gamma(parameters.gamma);
        else if (type() == OT_FG12) return TwoElectronFactory(world).dcut(1.e-7).BSH().gamma(parameters.gamma);
        else error("no kernel of type " + name() + " implemented");
        return TwoElectronFactory(world);
    }

    OpType type() const { return operator_type; }

    const Parameters parameters;

    std::shared_ptr<real_convolution_3d> get_op() const {return op;};

private:
    /// the world
    World& world;
    /// the operatortype, currently this can be g12_ or f12_
    const OpType operator_type = OT_UNDEFINED;

    /// @param[in] optype: can be f12_ or g12_ depending on which operator shall be intitialzied
    /// @param[in] parameters: parameters (thresholds etc)
    /// initializes the operators
    SeparatedConvolution<double, 3> *init_op(const OpType& type, const Parameters& parameters) const;

    std::shared_ptr<real_convolution_3d> op;
    intermediateT imH;
    intermediateT imP;
    intermediateT imR;

    /// @param[in] msg: output message
    /// the function will throw an MADNESS_EXCEPTION
    void error(const std::string& msg) const {
        if (world.rank() == 0)
            std::cout << "\n\n!!!!ERROR in CCConvolutionOperator " << assign_name(operator_type) << ": " << msg
                      << "!!!!!\n\n" << std::endl;
        MADNESS_EXCEPTION(msg.c_str(), 1);
    }
};

class CCPair : public archive::ParallelSerializableObject {
public:
    CCPair(){};

    CCPair(const size_t ii, const size_t jj, const CCState t, const CalcType c) : type(t), ctype(c), i(ii), j(jj),
                                                                                  bsh_eps(12345.6789) {};

    CCPair(const size_t ii, const size_t jj, const CCState t, const CalcType c, const std::vector<CCPairFunction>& f)
            : type(t), ctype(c), i(ii), j(jj), functions(f), bsh_eps(12345.6789) {};

    CCPair(const CCPair& other) : type(other.type), ctype(other.ctype), i(other.i), j(other.j),
                                  functions(other.functions), constant_part(other.constant_part),
                                  bsh_eps(other.bsh_eps) {};

    CCState type;
    CalcType ctype;
    size_t i;
    size_t j;

    /// gives back the pure 6D part of the pair function
    real_function_6d function() const {
        MADNESS_ASSERT(not functions.empty());
        MADNESS_ASSERT(functions[0].is_pure());
        return functions[0].get_function();
    }

    /// updates the pure 6D part of the pair function
    void update_u(const real_function_6d& u) {
        MADNESS_ASSERT(not functions.empty());
        MADNESS_ASSERT(functions[0].is_pure());
        CCPairFunction tmp(u);
        functions[0] = tmp;
    }

    template<typename Archive>
    void serialize(const Archive& ar) {
        size_t f_size = functions.size();
        bool fexist = (f_size > 0) && (functions[0].get_function().is_initialized());
        bool cexist = constant_part.is_initialized();
        ar & type & ctype & i & j & bsh_eps & fexist & cexist & f_size;
        if constexpr (Archive::is_input_archive) {
            if (fexist) {
                real_function_6d func;
                ar & func;
                CCPairFunction f1(func);
                functions.push_back(f1);
            }
        } else {
            if (fexist) ar & functions[0].get_function();
        }
        if (cexist) ar & constant_part;
    }

    bool load_pair(World& world) {
        std::string name = "pair_" + stringify(i) + stringify(j);
        bool exists = archive::ParallelInputArchive<archive::BinaryFstreamInputArchive>::exists(world, name.c_str());
        if (exists) {
            if (world.rank() == 0) printf("loading matrix elements %s\n", name.c_str());
            archive::ParallelInputArchive<archive::BinaryFstreamInputArchive> ar(world, name.c_str(), 1);
            ar & *this;
            //if (world.rank() == 0) printf(" %s\n", (converged) ? " converged" : " not converged");
            if (functions[0].get_function().is_initialized()) functions[0].get_function().set_thresh(FunctionDefaults<6>::get_thresh());
            if (constant_part.is_initialized()) constant_part.set_thresh(FunctionDefaults<6>::get_thresh());
        } else {
            if (world.rank() == 0) print("could not find pair ", i, j, " on disk");
        }
        return exists;
    }

    void store_pair(World& world) {
        std::string name = "pair_" + stringify(i) + stringify(j);
        if (world.rank() == 0) printf("storing matrix elements %s\n", name.c_str());
        archive::ParallelOutputArchive<archive::BinaryFstreamOutputArchive> ar(world, name.c_str(), 1);
        ar & *this;
    }

    hashT hash() const {
        hashT hash_i = std::hash<std::size_t>{}(i);
        hash_combine(hash_i, std::hash<std::size_t>{}(j));
        if (constant_part.is_initialized()) {
            hash_combine(hash_i, hash_value(constant_part.get_impl()->id()));
        }
        return hash_i;
    }

    /// the functions which belong to the pair
    std::vector<CCPairFunction> functions;

    /// the constant part
    real_function_6d constant_part;

    /// Energy for the BSH Operator
    /// Ground State: e_i + e_j
    /// Excited State: e_i + e_j + omega
    double bsh_eps;

    std::string name() const {
        std::string name = "???";
        if (type == GROUND_STATE) name = assign_name(ctype) + "_pair_u_";
        if (type == EXCITED_STATE) name = assign_name(ctype) + "_pair_x_";
        return name + stringify(i) + stringify(j);
    }

    void
    info() const;

};

/// little helper structure which manages the stored singles potentials
struct CCIntermediatePotentials {
    CCIntermediatePotentials(World& world, const CCParameters& p) : world(world), parameters(p) {};

    /// fetches the correct stored potential or throws an exception
    vector_real_function_3d
    operator()(const CC_vecfunction& f, const PotentialType& type) const;

    /// fetch the potential for a single function
    real_function_3d
    operator()(const CCFunction& f, const PotentialType& type) const;

    /// deltes all stored potentials
    void clear_all() {
        current_singles_potential_gs_.clear();
        current_singles_potential_ex_.clear();
        current_s2b_potential_gs_.clear();
        current_s2b_potential_ex_.clear();
        current_s2c_potential_gs_.clear();
        current_s2c_potential_ex_.clear();
    }

    /// clears only potentials of the response
    void clear_response() {
        current_singles_potential_ex_.clear();
        current_s2b_potential_ex_.clear();
        current_s2c_potential_ex_.clear();
    }

    /// insert potential
    void
    insert(const vector_real_function_3d& potential, const CC_vecfunction& f, const PotentialType& type);

private:
    World& world;
    const CCParameters& parameters;
    /// whole ground state singles potential without fock-residue
    vector_real_function_3d current_singles_potential_gs_;
    /// whole excited state singles potential without fock-residue
    vector_real_function_3d current_singles_potential_ex_;
    /// s2b_potential for the pure 6D-part of the ground-state (expensive and constant during singles iterations)
    vector_real_function_3d current_s2b_potential_gs_;
    /// s2b_potential for the pure 6D-part of the excited-state (expensive and constant during singles iterations)
    vector_real_function_3d current_s2b_potential_ex_;
    /// s2c_potential for the pure 6D-part of the ground-state (expensive and constant during singles iterations)
    vector_real_function_3d current_s2c_potential_gs_;
    /// s2c_potential for the pure 6D-part of the excited_state (expensive and constant during singles iterations)
    vector_real_function_3d current_s2c_potential_ex_;
    /// unprojected S3c + S5c + S2b + S2c potential of CC2 singles
    /// for the projector response of the CC2 singles potential
    vector_real_function_3d unprojected_cc2_projector_response_;

    /// structured output
    void output(const std::string& msg) const {
        if (world.rank() == 0 and parameters.debug())
            std::cout << "Intermediate Potential Manager: " << msg << "\n";
    }
};

class MacroTaskMp2ConstantPart : public MacroTaskOperationBase {

    class ConstantPartPartitioner : public MacroTaskPartitioner {
    public:
        ConstantPartPartitioner() {};

        partitionT do_partitioning(const std::size_t& vsize1, const std::size_t& vsize2,
                                   const std::string policy) const override {
            partitionT p;
            for (int i = 0; i < vsize1; i++) {
                Batch batch(Batch_1D(i,i+1), Batch_1D(i,i+1));
                p.push_back(std::make_pair(batch,1.0));
            }
            return p;
        }
    };

public:
    MacroTaskMp2ConstantPart(){partitioner.reset(new ConstantPartPartitioner());}

    typedef std::tuple<const std::vector<CCPair>&, const std::vector<real_function_3d>&,
            const std::vector<real_function_3d>&, const CCParameters&, const real_function_3d&,
            const std::vector<real_function_3d>&, const std::vector<std::string>& > argtupleT;

    using resultT = std::vector<real_function_6d>;

    resultT allocator(World& world, const argtupleT& argtuple) const {
        std::size_t n = std::get<0>(argtuple).size();
        resultT result = zero_functions_compressed<double, 6>(world, n);
        return result;
    }

    resultT operator() (const std::vector<CCPair>& pair, const std::vector<real_function_3d>& mo_ket,
                        const std::vector<real_function_3d>& mo_bra, const CCParameters& parameters,
                        const real_function_3d& Rsquare, const std::vector<real_function_3d>& U1,
                        const std::vector<std::string>& argument) const;
};

class MacroTaskMp2UpdatePair : public MacroTaskOperationBase {

    class UpdatePairPartitioner : public MacroTaskPartitioner {
    public :
        UpdatePairPartitioner() {
            set_dimension(2);
        }

        partitionT do_partitioning(const std::size_t& vsize1, const std::size_t& vsize2,
                                   const std::string policy) const override {
            partitionT p;
            for (int i = 0; i < vsize1; i++) {
                Batch batch(Batch_1D(i, i+1), Batch_1D(i, i+1), Batch_1D(i,i+1));
                p.push_back(std::make_pair(batch, 1.0));
            }
            return p;
        }
    };
public:
    MacroTaskMp2UpdatePair() {partitioner.reset(new UpdatePairPartitioner());}

    typedef std::tuple<const std::vector<CCPair>&, const std::vector<real_function_6d>&, const CCParameters&,
                        const std::vector< madness::Vector<double,3> >&,
                       const std::vector<real_function_3d>&, const std::vector<real_function_3d>&,
                       const std::vector<real_function_3d>&, const real_function_3d&> argtupleT;

    using resultT = std::vector<real_function_6d>;

    resultT allocator(World& world, const argtupleT& argtuple) const {
        std::size_t n = std::get<0>(argtuple).size();
        resultT result = zero_functions_compressed<double, 6>(world, n);
        return result;
    }

    resultT operator() (const std::vector<CCPair>& pair, const std::vector<real_function_6d>& mp2_coupling, const CCParameters& parameters,
                        const std::vector< madness::Vector<double,3> >& all_coords_vec,
                        const std::vector<real_function_3d>& mo_ket, const std::vector<real_function_3d>& mo_bra,
                        const std::vector<real_function_3d>& U1, const real_function_3d& U2) const;
};

}//namespace madness

#endif /* CCSTRUCTURES_H_ */
