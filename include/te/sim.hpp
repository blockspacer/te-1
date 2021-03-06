#ifndef TE_SIM_HPP_INCLUDED
#define TE_SIM_HPP_INCLUDED

#include <te/util.hpp>
#include <unordered_map>
#include <vector>
#include <random>
#include <string>
#include <glm/vec2.hpp>
#include <entt/entt.hpp>

namespace te {
    struct family {
        double balance;
    };

    struct named {
        std::string name;
    };

    struct price {
        double price;
    };

    struct footprint {
        glm::vec2 dimensions;
    };

    struct site {
        glm::vec2 position;
    };

    struct ghost {
        entt::entity proto;
    };

    struct dweller {
        //TODO: programmatically represent requirements of living
        // i.e. dwellings need 2 of 3 food types in abundance
    };

    // A demander stores the rate of increase of demand of entities
    struct demander {
        std::unordered_map<entt::entity, double> rate;
    };

    // A trader stores the current demand of entities
    struct trader {
        // the index of the family this trader works for
        unsigned int family_ix;
        // +ve bid = buying
        // -ve bid = selling
        std::unordered_map<entt::entity, double> bid;
        double balance = 0.0;
    };

    struct generator {
        entt::entity output;
        double rate;
        double progress = 0.0;
    };

    struct producer {
        std::unordered_map<entt::entity, double> inputs;
        std::unordered_map<entt::entity, double> outputs;
        double rate;
        bool producing = false;
        double progress = 0.0;
    };

    struct inventory {
        std::unordered_map<entt::entity, int> stock;
    };

    struct market {
        std::unordered_map<entt::entity, double> prices;
        std::unordered_map<entt::entity, double> demand;
        entt::entity commons;
        double radius = 5.0f;
        int population = 0;
        double growth_rate = 0.001;
        double growth = 0.0;
    };

    struct stop {
        entt::entity where;
        std::unordered_map<entt::entity, int> leave_with;
    };

    struct route {
        std::string name;
        std::vector<stop> stops;
    };

    struct merchant {
        std::optional<te::route> route;
        std::size_t last_stop = 0;
        bool trading = false;
    };

    struct sim {
        std::default_random_engine rengine;
        
        entt::registry entities;
        std::vector<family> families;
        std::vector<entt::entity> commodities;
        std::vector<entt::entity> blueprints;
        std::vector<route> routes;
        entt::entity merchant_blueprint;

        const int map_width = 40;
        const int map_height = 40;
        std::unordered_map<glm::ivec2, entt::entity> grid;
        glm::vec2 snap(glm::vec2 pos, glm::vec2 print) const;

        sim(unsigned seed);

        void init_blueprints();
        void generate_map();

        // which entities a market has influence over
        std::unordered_map<entt::entity, std::vector<entt::entity>> market_influencees;
        // which markets an entity is influenced by
        std::unordered_map<entt::entity, std::vector<entt::entity>> influencee_markets;

        // total units wanting to be sold
        int market_stock(entt::entity market_e, entt::entity commodity_e);
        int market_demand(entt::entity market_e, entt::entity commodity_e);

        market* market_at(glm::vec2 x);
        bool in_market(const site& question_site, const site& market_site, const market& the_market) const;

        bool can_place(entt::entity entity, glm::vec2 where);
        std::optional<entt::entity> try_place(entt::entity entity, glm::vec2 where);
        
        bool spawn_dwelling(entt::entity market);
        void spawn(entt::entity proto);
        
        void tick(double delta_t);
    };

    //TOOD: put these somewhere else
    // Client Components
    struct render_tex {
        std::string filename;
    };
    struct render_mesh {
        std::string filename;
    };
    struct pickable {
    };
}

#endif
