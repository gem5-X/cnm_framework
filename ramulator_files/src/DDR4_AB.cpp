#include "DDR4_AB.h"
#include "DRAM.h"

#include <vector>
#include <functional>
#include <cassert>

using namespace std;
using namespace ramulator;

string DDR4_AB::standard_name = "DDR4_AB";
string DDR4_AB::level_str [int(Level::MAX)] = {"Ch", "Ra", "Bg", "Ba", "Ro", "Co"};

map<string, enum DDR4_AB::Org> DDR4_AB::org_map = {
    {"DDR4_2Gb_x4", DDR4_AB::Org::DDR4_2Gb_x4}, {"DDR4_2Gb_x8", DDR4_AB::Org::DDR4_2Gb_x8}, {"DDR4_2Gb_x16", DDR4_AB::Org::DDR4_2Gb_x16},
    {"DDR4_4Gb_x4", DDR4_AB::Org::DDR4_4Gb_x4}, {"DDR4_4Gb_x8", DDR4_AB::Org::DDR4_4Gb_x8}, {"DDR4_4Gb_x16", DDR4_AB::Org::DDR4_4Gb_x16},
    {"DDR4_8Gb_x4", DDR4_AB::Org::DDR4_8Gb_x4}, {"DDR4_8Gb_x8", DDR4_AB::Org::DDR4_8Gb_x8}, {"DDR4_8Gb_x16", DDR4_AB::Org::DDR4_8Gb_x16},
};

map<string, enum DDR4_AB::Speed> DDR4_AB::speed_map = {
    {"DDR4_1600K", DDR4_AB::Speed::DDR4_1600K}, {"DDR4_1600L", DDR4_AB::Speed::DDR4_1600L},
    {"DDR4_1866M", DDR4_AB::Speed::DDR4_1866M}, {"DDR4_1866N", DDR4_AB::Speed::DDR4_1866N},
    {"DDR4_2133P", DDR4_AB::Speed::DDR4_2133P}, {"DDR4_2133R", DDR4_AB::Speed::DDR4_2133R},
    {"DDR4_2400R", DDR4_AB::Speed::DDR4_2400R}, {"DDR4_2400U", DDR4_AB::Speed::DDR4_2400U},
    {"DDR4_3200", DDR4_AB::Speed::DDR4_3200},   {"DDR4_400MHz", DDR4_AB::Speed::DDR4_400MHz}
};


DDR4_AB::DDR4_AB(Org org, Speed speed)
    : org_entry(org_table[int(org)]),
    speed_entry(speed_table[int(speed)]), 
    read_latency(speed_entry.nCL + speed_entry.nBL)
{
    init_speed();
    init_prereq();
    init_rowhit(); // SAUGATA: added row hit function
    init_rowopen();
    init_lambda();
    init_timing();
}

DDR4_AB::DDR4_AB(const string& org_str, const string& speed_str) :
    DDR4_AB(org_map[org_str], speed_map[speed_str]) 
{
}

void DDR4_AB::set_channel_number(int channel) {
  org_entry.count[int(Level::Channel)] = channel;
}

void DDR4_AB::set_rank_number(int rank) {
  org_entry.count[int(Level::Rank)] = rank;
}

void DDR4_AB::init_speed()
{
    const static int RRDS_TABLE[2][6] = {
        {4, 4, 4, 4, 4, 4},
        {5, 5, 6, 7, 9, 4}
    };
    const static int RRDL_TABLE[2][6] = {
        {5, 5, 6, 6, 8, 4},
        {6, 6, 7, 8, 11, 5}
    };
    const static int FAW_TABLE[3][6] = {
        {16, 16, 16, 16, 16, 16},
        {20, 22, 23, 26, 34, 16},
        {28, 28, 32, 36, 48, 16}
    };
    const static int RFC_TABLE[int(RefreshMode::MAX)][3][6] = {{   
            {128, 150, 171, 192, 256, 64},
            {208, 243, 278, 312, 416, 104},
            {280, 327, 374, 420, 560, 140}
        },{
            {88, 103, 118, 132,  176, 44},
            {128, 150, 171, 192, 256, 64},
            {208, 243, 278, 312, 416, 104} 
        },{
            {72, 84, 96, 108, 144, 36},
            {88, 103, 118, 132, 33},
            {128, 150, 171, 192, 48}  
        }
    };
    const static int REFI_TABLE[6] = {
        6240, 7280, 8320, 9360, 12480, 3120
    };
    const static int XS_TABLE[3][6] = {
        {136, 159, 182, 204, 272, 68},
        {216, 252, 288, 324, 432, 108},
        {288, 336, 384, 432, 576, 144}
    };

    int speed = 0, density = 0;
    switch (speed_entry.rate) {
        case 1600: speed = 0; break;
        case 1866: speed = 1; break;
        case 2133: speed = 2; break;
        case 2400: speed = 3; break;
        case 3200: speed = (speed_entry.freq==1600) ? 4 : 5; break;
        default: assert(false);
    };
    switch (org_entry.size >> 10){
        case 2: density = 0; break;
        case 4: density = 1; break;
        case 8: density = 2; break;
        default: assert(false);
    }
    speed_entry.nRRDS = RRDS_TABLE[org_entry.dq == 16? 1: 0][speed];
    speed_entry.nRRDL = RRDL_TABLE[org_entry.dq == 16? 1: 0][speed];
    speed_entry.nFAW = FAW_TABLE[org_entry.dq == 4? 0: org_entry.dq == 8? 1: 2][speed];
    speed_entry.nRFC = RFC_TABLE[(int)refresh_mode][density][speed];
    speed_entry.nREFI = (REFI_TABLE[speed] >> int(refresh_mode));
    speed_entry.nXS = XS_TABLE[density][speed];
}


void DDR4_AB::init_prereq()
{
    // RD
    prereq[int(Level::Rank)][int(Command::RD)] = [] (DRAM<DDR4_AB>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::PowerUp): return Command::MAX;
            case int(State::ActPowerDown): return Command::PDX;
            case int(State::PrePowerDown): return Command::PDX;
            case int(State::SelfRefresh): return Command::SRX;
            default: assert(false);
        }};
    prereq[int(Level::Bank)][int(Command::RD)] = [] (DRAM<DDR4_AB>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::Closed): return Command::ACT;
            case int(State::Opened):
                if (node->row_state.find(id) != node->row_state.end())
                    return cmd;
                else return Command::PRE;
            default: assert(false);
        }};

    // WR
    prereq[int(Level::Rank)][int(Command::WR)] = prereq[int(Level::Rank)][int(Command::RD)];
    prereq[int(Level::Bank)][int(Command::WR)] = prereq[int(Level::Bank)][int(Command::RD)];

    // REF
    prereq[int(Level::Rank)][int(Command::REF)] = [] (DRAM<DDR4_AB>* node, Command cmd, int id) {
        for (auto bg : node->children)
            for (auto bank: bg->children) {
                if (bank->state == State::Closed)
                    continue;
                return Command::PREA;
            }
        return Command::REF;};

    // PD
    prereq[int(Level::Rank)][int(Command::PDE)] = [] (DRAM<DDR4_AB>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::PowerUp): return Command::PDE;
            case int(State::ActPowerDown): return Command::PDE;
            case int(State::PrePowerDown): return Command::PDE;
            case int(State::SelfRefresh): return Command::SRX;
            default: assert(false);
        }};

    // SR
    prereq[int(Level::Rank)][int(Command::SRE)] = [] (DRAM<DDR4_AB>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::PowerUp): return Command::SRE;
            case int(State::ActPowerDown): return Command::PDX;
            case int(State::PrePowerDown): return Command::PDX;
            case int(State::SelfRefresh): return Command::SRE;
            default: assert(false);
        }};
}

// SAUGATA: added row hit check functions to see if the desired location is currently open
void DDR4_AB::init_rowhit()
{
    // RD
    rowhit[int(Level::Bank)][int(Command::RD)] = [] (DRAM<DDR4_AB>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::Closed): return false;
            case int(State::Opened):
                if (node->row_state.find(id) != node->row_state.end())
                    return true;
                return false;
            default: assert(false);
        }};

    // WR
    rowhit[int(Level::Bank)][int(Command::WR)] = rowhit[int(Level::Bank)][int(Command::RD)];
}

void DDR4_AB::init_rowopen()
{
    // RD
    rowopen[int(Level::Bank)][int(Command::RD)] = [] (DRAM<DDR4_AB>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::Closed): return false;
            case int(State::Opened): return true;
            default: assert(false);
        }};

    // WR
    rowopen[int(Level::Bank)][int(Command::WR)] = rowopen[int(Level::Bank)][int(Command::RD)];
}

void DDR4_AB::init_lambda()
{
    lambda[int(Level::Bank)][int(Command::ACT)] = [] (DRAM<DDR4_AB>* node, int id) {
        for (auto bg : node->parent->parent->children)  // Open row at all banks at every BG
            for (auto bank: bg->children) {
                bank->state = State::Opened;
                bank->row_state[id] = State::Opened;}};
    lambda[int(Level::Bank)][int(Command::PRE)] = [] (DRAM<DDR4_AB>* node, int id) {
        for (auto bg : node->parent->parent->children)  // Close row at all banks at every BG
            for (auto bank: bg->children) {
                bank->state = State::Closed;
                bank->row_state.clear();}};
    lambda[int(Level::Rank)][int(Command::PREA)] = [] (DRAM<DDR4_AB>* node, int id) {
        for (auto bg : node->children)
            for (auto bank : bg->children) {
                bank->state = State::Closed;
                bank->row_state.clear();
            }};
    lambda[int(Level::Rank)][int(Command::REF)] = [] (DRAM<DDR4_AB>* node, int id) {};
    lambda[int(Level::Bank)][int(Command::RD)] = [] (DRAM<DDR4_AB>* node, int id) {};
    lambda[int(Level::Bank)][int(Command::WR)] = [] (DRAM<DDR4_AB>* node, int id) {};
    lambda[int(Level::Bank)][int(Command::RDA)] = [] (DRAM<DDR4_AB>* node, int id) {
        for (auto bg : node->parent->parent->children)  // Close row at all banks at every BG
            for (auto bank: bg->children) {
                bank->state = State::Closed;
                bank->row_state.clear();}};
    lambda[int(Level::Bank)][int(Command::WRA)] = [] (DRAM<DDR4_AB>* node, int id) {
        for (auto bg : node->parent->parent->children)  // Close row at all banks at every BG
            for (auto bank: bg->children) {
                bank->state = State::Closed;
                bank->row_state.clear();}};
    lambda[int(Level::Rank)][int(Command::PDE)] = [] (DRAM<DDR4_AB>* node, int id) {
        for (auto bg : node->children)
            for (auto bank : bg->children) {
                if (bank->state == State::Closed)
                    continue;
                node->state = State::ActPowerDown;
                return;
            }
        node->state = State::PrePowerDown;};
    lambda[int(Level::Rank)][int(Command::PDX)] = [] (DRAM<DDR4_AB>* node, int id) {
        node->state = State::PowerUp;};
    lambda[int(Level::Rank)][int(Command::SRE)] = [] (DRAM<DDR4_AB>* node, int id) {
        node->state = State::SelfRefresh;};
    lambda[int(Level::Rank)][int(Command::SRX)] = [] (DRAM<DDR4_AB>* node, int id) {
        node->state = State::PowerUp;};
}


void DDR4_AB::init_timing()  // @NOTE all banks of all BGs in a rank are accessed, so timings of BGs and banks at the rank level
{
    SpeedEntry& s = speed_entry;
    vector<TimingEntry> *t;

    /*** Channel ***/ 
    t = timing[int(Level::Channel)];

    // CAS <-> CAS
    t[int(Command::RD)].push_back({Command::RD, 1, s.nBL});
    t[int(Command::RD)].push_back({Command::RDA, 1, s.nBL});
    t[int(Command::RDA)].push_back({Command::RD, 1, s.nBL});
    t[int(Command::RDA)].push_back({Command::RDA, 1, s.nBL});
    t[int(Command::WR)].push_back({Command::WR, 1, s.nBL});
    t[int(Command::WR)].push_back({Command::WRA, 1, s.nBL});
    t[int(Command::WRA)].push_back({Command::WR, 1, s.nBL});
    t[int(Command::WRA)].push_back({Command::WRA, 1, s.nBL});


    /*** Rank ***/ 
    t = timing[int(Level::Rank)];

    // CAS <-> CAS  // In AllBanks mode, we never have nCCDS or nWTRS, only nCCDL and nWTRL
    t[int(Command::RD)].push_back({Command::RD, 1, s.nCCDL});
    t[int(Command::RD)].push_back({Command::RDA, 1, s.nCCDL});
    t[int(Command::RDA)].push_back({Command::RD, 1, s.nCCDL});
    t[int(Command::RDA)].push_back({Command::RDA, 1, s.nCCDL});
    t[int(Command::WR)].push_back({Command::WR, 1, s.nCCDL});
    t[int(Command::WR)].push_back({Command::WRA, 1, s.nCCDL});
    t[int(Command::WRA)].push_back({Command::WR, 1, s.nCCDL});
    t[int(Command::WRA)].push_back({Command::WRA, 1, s.nCCDL});
    t[int(Command::RD)].push_back({Command::WR, 1, s.nCL + s.nBL + 2 - s.nCWL});
    t[int(Command::RD)].push_back({Command::WRA, 1, s.nCL + s.nBL + 2 - s.nCWL});
    t[int(Command::RDA)].push_back({Command::WR, 1, s.nCL + s.nBL + 2 - s.nCWL});
    t[int(Command::RDA)].push_back({Command::WRA, 1, s.nCL + s.nBL + 2 - s.nCWL});
    t[int(Command::WR)].push_back({Command::RD, 1, s.nCWL + s.nBL + s.nWTRL});
    t[int(Command::WR)].push_back({Command::RDA, 1, s.nCWL + s.nBL + s.nWTRL});
    t[int(Command::WRA)].push_back({Command::RD, 1, s.nCWL + s.nBL + s.nWTRL});
    t[int(Command::WRA)].push_back({Command::RDA, 1, s.nCWL + s.nBL + s.nWTRL});

    // CAS <-> CAS (between sibling ranks)
    t[int(Command::RD)].push_back({Command::RD, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RD)].push_back({Command::RDA, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RDA)].push_back({Command::RD, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RDA)].push_back({Command::RDA, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RD)].push_back({Command::WR, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RD)].push_back({Command::WRA, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RDA)].push_back({Command::WR, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RDA)].push_back({Command::WRA, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RD)].push_back({Command::WR, 1, s.nCL + s.nBL + s.nRTRS - s.nCWL, true});
    t[int(Command::RD)].push_back({Command::WRA, 1, s.nCL + s.nBL + s.nRTRS - s.nCWL, true});
    t[int(Command::RDA)].push_back({Command::WR, 1, s.nCL + s.nBL + s.nRTRS - s.nCWL, true});
    t[int(Command::RDA)].push_back({Command::WRA, 1, s.nCL + s.nBL + s.nRTRS - s.nCWL, true});
    t[int(Command::WR)].push_back({Command::RD, 1, s.nCWL + s.nBL + s.nRTRS - s.nCL, true});
    t[int(Command::WR)].push_back({Command::RDA, 1, s.nCWL + s.nBL + s.nRTRS - s.nCL, true});
    t[int(Command::WRA)].push_back({Command::RD, 1, s.nCWL + s.nBL + s.nRTRS - s.nCL, true});
    t[int(Command::WRA)].push_back({Command::RDA, 1, s.nCWL + s.nBL + s.nRTRS - s.nCL, true});

    t[int(Command::RD)].push_back({Command::PREA, 1, s.nRTP});
    t[int(Command::WR)].push_back({Command::PREA, 1, s.nCWL + s.nBL + s.nWR});

    // CAS <-> RAS  // Upgraded from Bank level to Rank level
    t[int(Command::ACT)].push_back({Command::RD, 1, s.nRCD});
    t[int(Command::ACT)].push_back({Command::RDA, 1, s.nRCD});
    t[int(Command::ACT)].push_back({Command::WR, 1, s.nRCD});
    t[int(Command::ACT)].push_back({Command::WRA, 1, s.nRCD});

    t[int(Command::RD)].push_back({Command::PRE, 1, s.nRTP});
    t[int(Command::WR)].push_back({Command::PRE, 1, s.nCWL + s.nBL + s.nWR});

    t[int(Command::RDA)].push_back({Command::ACT, 1, s.nRTP + s.nRP});
    t[int(Command::WRA)].push_back({Command::ACT, 1, s.nCWL + s.nBL + s.nWR + s.nRP});

    // CAS <-> PD
    t[int(Command::RD)].push_back({Command::PDE, 1, s.nCL + s.nBL + 1});
    t[int(Command::RDA)].push_back({Command::PDE, 1, s.nCL + s.nBL + 1});
    t[int(Command::WR)].push_back({Command::PDE, 1, s.nCWL + s.nBL + s.nWR});
    t[int(Command::WRA)].push_back({Command::PDE, 1, s.nCWL + s.nBL + s.nWR + 1}); // +1 for pre
    t[int(Command::PDX)].push_back({Command::RD, 1, s.nXP});
    t[int(Command::PDX)].push_back({Command::RDA, 1, s.nXP});
    t[int(Command::PDX)].push_back({Command::WR, 1, s.nXP});
    t[int(Command::PDX)].push_back({Command::WRA, 1, s.nXP});
    
    // CAS <-> SR: none (all banks have to be precharged)

    // RAS <-> RAS  // In AllBanks mode, we never have nRRDS, only nRRDL
                    // Some upgrades from Bank level to Rank level
    t[int(Command::ACT)].push_back({Command::ACT, 1, s.nRRDL});
    t[int(Command::ACT)].push_back({Command::ACT, 4, s.nFAW});
    t[int(Command::ACT)].push_back({Command::ACT, 1, s.nRC});
    t[int(Command::ACT)].push_back({Command::PREA, 1, s.nRAS});
    t[int(Command::ACT)].push_back({Command::PRE, 1, s.nRAS});
    t[int(Command::PREA)].push_back({Command::ACT, 1, s.nRP});
    t[int(Command::PRE)].push_back({Command::ACT, 1, s.nRP});

    // RAS <-> REF
    t[int(Command::ACT)].push_back({Command::REF, 1, s.nRC});
    t[int(Command::PRE)].push_back({Command::REF, 1, s.nRP});
    t[int(Command::PREA)].push_back({Command::REF, 1, s.nRP});
    t[int(Command::RDA)].push_back({Command::REF, 1, s.nRTP + s.nRP});
    t[int(Command::WRA)].push_back({Command::REF, 1, s.nCWL + s.nBL + s.nWR + s.nRP});
    t[int(Command::REF)].push_back({Command::ACT, 1, s.nRFC});

    // RAS <-> PD
    t[int(Command::ACT)].push_back({Command::PDE, 1, 1});
    t[int(Command::PDX)].push_back({Command::ACT, 1, s.nXP});
    t[int(Command::PDX)].push_back({Command::PRE, 1, s.nXP});
    t[int(Command::PDX)].push_back({Command::PREA, 1, s.nXP});

    // RAS <-> SR
    t[int(Command::PRE)].push_back({Command::SRE, 1, s.nRP});
    t[int(Command::PREA)].push_back({Command::SRE, 1, s.nRP});
    t[int(Command::SRX)].push_back({Command::ACT, 1, s.nXS});

    // REF <-> REF
    t[int(Command::REF)].push_back({Command::REF, 1, s.nRFC});

    // REF <-> PD
    t[int(Command::REF)].push_back({Command::PDE, 1, 1});
    t[int(Command::PDX)].push_back({Command::REF, 1, s.nXP});

    // REF <-> SR
    t[int(Command::SRX)].push_back({Command::REF, 1, s.nXS});
    
    // PD <-> PD
    t[int(Command::PDE)].push_back({Command::PDX, 1, s.nPD});
    t[int(Command::PDX)].push_back({Command::PDE, 1, s.nXP});

    // PD <-> SR
    t[int(Command::PDX)].push_back({Command::SRE, 1, s.nXP});
    t[int(Command::SRX)].push_back({Command::PDE, 1, s.nXS});
    
    // SR <-> SR
    t[int(Command::SRE)].push_back({Command::SRX, 1, s.nCKESR});
    t[int(Command::SRX)].push_back({Command::SRE, 1, s.nXS});

    // /*** Bank Group ***/ 
    // t = timing[int(Level::BankGroup)];
    // // CAS <-> CAS
    // t[int(Command::RD)].push_back({Command::RD, 1, s.nCCDL});
    // t[int(Command::RD)].push_back({Command::RDA, 1, s.nCCDL});
    // t[int(Command::RDA)].push_back({Command::RD, 1, s.nCCDL});
    // t[int(Command::RDA)].push_back({Command::RDA, 1, s.nCCDL});
    // t[int(Command::WR)].push_back({Command::WR, 1, s.nCCDL});
    // t[int(Command::WR)].push_back({Command::WRA, 1, s.nCCDL});
    // t[int(Command::WRA)].push_back({Command::WR, 1, s.nCCDL});
    // t[int(Command::WRA)].push_back({Command::WRA, 1, s.nCCDL});
    // t[int(Command::WR)].push_back({Command::RD, 1, s.nCWL + s.nBL + s.nWTRL});
    // t[int(Command::WR)].push_back({Command::RDA, 1, s.nCWL + s.nBL + s.nWTRL});
    // t[int(Command::WRA)].push_back({Command::RD, 1, s.nCWL + s.nBL + s.nWTRL});
    // t[int(Command::WRA)].push_back({Command::RDA, 1, s.nCWL + s.nBL + s.nWTRL});

    // // RAS <-> RAS
    // t[int(Command::ACT)].push_back({Command::ACT, 1, s.nRRDL});

    // /*** Bank ***/ 
    // t = timing[int(Level::Bank)];

    // // CAS <-> RAS
    // t[int(Command::ACT)].push_back({Command::RD, 1, s.nRCD});
    // t[int(Command::ACT)].push_back({Command::RDA, 1, s.nRCD});
    // t[int(Command::ACT)].push_back({Command::WR, 1, s.nRCD});
    // t[int(Command::ACT)].push_back({Command::WRA, 1, s.nRCD});

    // t[int(Command::RD)].push_back({Command::PRE, 1, s.nRTP});
    // t[int(Command::WR)].push_back({Command::PRE, 1, s.nCWL + s.nBL + s.nWR});

    // t[int(Command::RDA)].push_back({Command::ACT, 1, s.nRTP + s.nRP});
    // t[int(Command::WRA)].push_back({Command::ACT, 1, s.nCWL + s.nBL + s.nWR + s.nRP});

    // // RAS <-> RAS
    // t[int(Command::ACT)].push_back({Command::ACT, 1, s.nRC});
    // t[int(Command::ACT)].push_back({Command::PRE, 1, s.nRAS});
    // t[int(Command::PRE)].push_back({Command::ACT, 1, s.nRP});
}
