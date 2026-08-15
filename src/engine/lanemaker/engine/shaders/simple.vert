#version 330 core

// PBR lighting vertex shader
// Computes per-vertex lighting with directional sun + sky ambient

layout(location = 0) in vec3 position;   // vertex position
layout(location = 1) in vec3 normal;      // vertex normal
layout(location = 2) in vec3 color;       // vertex color (base albedo)
layout(location = 3) in float objectID;   // object ID for highlight/hidden flags

out vec4 fragColor;                       // computed lit color
out vec3 vNormal;
out vec3 vWorldPos;

uniform mat4 worldToView;
uniform sampler1D objectInfo;

// Lighting uniforms
uniform vec3 cameraPos;
uniform vec3 lightDir;      // direction TO the light (sun)
uniform vec3 lightColor;    // sun color/intensity
uniform vec3 ambientColor;  // sky ambient color/intensity
uniform int  viewMode3D;    // 1 = 3D perspective, 0 = 2D top-down

void main() {
    int objectID_I = int(round(objectID));
    float objFlag = objectID_I == -1 ? 0 : texelFetch(objectInfo, objectID_I, 0).r;
    int objFlag_I = int(round(objFlag * 16));

    vec3 adjustedPosition = position;
    vec3 baseColor = color;

    // Object flags: highlight (bit 0), hidden (bit 1), green light (bit 2)
    bool highlighted = (objFlag_I & 1) != 0;
    bool hidden      = (objFlag_I & 2) != 0;
    bool greenLight  = (objFlag_I & 4) != 0;

    if (highlighted) {
        baseColor = baseColor * 1.5;
        adjustedPosition.z = position.z + 0.02;
    }

    if (greenLight) {
        baseColor = baseColor * vec3(0.5, 2.0, 0.5);
        adjustedPosition.z = position.z + 0.02;
    }

    // Pass world-space data to fragment shader
    vNormal = normalize(normal);
    vWorldPos = adjustedPosition;

    if (hidden) {
        gl_Position = vec4(0, 0, 0, 0);
        fragColor = vec4(0);
    } else {
        gl_Position = worldToView * vec4(adjustedPosition, 1.0);

        if (viewMode3D == 1) {
            // === 3D PBR Lighting ===
            vec3 N = normalize(normal);
            vec3 L = normalize(lightDir);
            vec3 V = normalize(cameraPos - adjustedPosition);
            vec3 H = normalize(L + V);

            // Diffuse (Lambert)
            float NdotL = max(dot(N, L), 0.0);

            // Specular (Blinn-Phong, simplified PBR-ish)
            float NdotH = max(dot(N, H), 0.0);
            float specPower = 32.0;
            float specular = pow(NdotH, specPower);

            // Fresnel-like rim lighting for edges
            float NdotV = max(dot(N, V), 0.0);
            float rim = pow(1.0 - NdotV, 3.0) * 0.3;

            // Sky ambient (hemispheric: sky from top, ground bounce from bottom)
            float skyFactor = N.z * 0.5 + 0.5;  // z-up: 1=up, 0=down
            vec3 ambient = mix(ambientColor * 0.3, ambientColor, skyFactor);

            // Combine
            vec3 lit = baseColor * (ambient + lightColor * NdotL)
                     + lightColor * specular * 0.15
                     + ambientColor * rim;

            // Subtle tone mapping (Reinhard)
            lit = lit / (lit + vec3(1.0));
            // Gamma correction
            lit = pow(lit, vec3(1.0/2.2));

            fragColor = vec4(lit, 1.0);
        } else {
            // 2D top-down: flat color, slight brightness
            fragColor = vec4(baseColor * 0.9, 1.0);
        }
    }
}
