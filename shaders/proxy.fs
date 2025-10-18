#version 330 core

// --- ADD THESE UNIFORMS ---
uniform vec3 u_cameraPosition;
uniform sampler3D u_volumeTexture;
uniform float u_stepSize = 0.005; // How far to step each time
uniform int u_marchSteps = 256;   // Max steps to take

// --- ADD THESE INPUTS (must match vertex shader outputs) ---
in vec3 fragWorldPos;
in vec3 texCoord;

out vec4 out_Color;

vec4 transferFunction(float scalar_value) {
    if (scalar_value < 0.1) {
        return vec4(0.0);
    }
    vec3 color = mix(vec3(0.1, 0.2, 1.0), vec3(1.0, 1.0, 1.0), scalar_value);
    float alpha = mix(0.01, 0.4, scalar_value);
    return vec4(color, alpha);
}

void main() {
    // The ray starts at the camera and goes towards the fragment on the cube's surface
    vec3 rayDir = normalize(fragWorldPos - u_cameraPosition);
    
    // We start marching from the texture coordinate provided by the vertex shader
    vec3 currentPos = texCoord;

    vec4 accumulatedColor = vec4(0.0);

    for (int i = 0; i < u_marchSteps; i++) {
        // Check if we are still inside the texture bounds [0, 1]
        if (any(lessThan(currentPos, vec3(0.0))) || any(greaterThan(currentPos, vec3(1.0)))) {
            break;
        }

        float scalar = texture(u_volumeTexture, currentPos).r;
        vec4 sampleData = transferFunction(scalar);
        
        // Front-to-back compositing ("over" operator)
        // new_color = old_color + (1 - old_alpha) * sample_alpha * sample_color
        // new_alpha = old_alpha + (1 - old_alpha) * sample_alpha
        accumulatedColor.rgb += (1.0 - accumulatedColor.a) * sampleData.a * sampleData.rgb;
        accumulatedColor.a += (1.0 - accumulatedColor.a) * sampleData.a;
        
        // Early exit if the color is fully opaque
        if (accumulatedColor.a > 0.99) {
            break;
        }

        // Move to the next position along the ray
        currentPos += rayDir * u_stepSize;
    }

    out_Color = accumulatedColor;
}