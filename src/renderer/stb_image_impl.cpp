// Unite de compilation qui instancie l'implementation de stb_image
// (chargement PNG/JPG des sprites). A inclure une seule fois dans le projet.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD // evite des soucis de portabilite a la compilation
#include <stb_image.h>
