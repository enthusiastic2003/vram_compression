#version 330 core
layout (location = 0) in vec3 aPos; // Vertex position in object space [-0.5, 0.5]

// --- ADD THESE UNIFORMS ---
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// --- ADD THESE OUTPUTS ---
out vec3 fragWorldPos; // The fragment's position in world space
out vec3 texCoord;     // The texture coordinate in [0, 1] range

void main() {
    // Calculate the final screen position of the vertex
    gl_Position = projection * view * model * vec4(aPos, 1.0);

    // Pass the world position of the fragment to the next stage
    fragWorldPos = (model * vec4(aPos, 1.0)).xyz;
    
        // Convert object position to [0, 1] texture coordinate and pass it
    
        texCoord = aPos + 0.5;
    
    }
    
    