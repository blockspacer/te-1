#ifndef TE_CAMERA_HPP_INCLUDED
#define TE_CAMERA_HPP_INCLUDED

#include <glm/glm.hpp>

namespace te {
    struct ray {
        glm::vec3 origin;
        glm::vec3 direction;
    };
    struct camera {
        glm::vec3 focus;
        glm::vec3 offset;
        glm::vec3 eye() const;
        glm::vec3 forward() const;
        ray cast(glm::vec3 ndc) const;
        float zoom_factor;
        void zoom(float delta);
        float aspect_ratio;
        glm::mat4 view() const;
        glm::mat4 projection() const;
        bool use_ortho = true;
    };
}

#endif // TE_CAMERA_HPP_INCLUDED
