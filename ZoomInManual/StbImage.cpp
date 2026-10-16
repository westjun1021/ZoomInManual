// StbImage.cpp
// stb_image 의 구현부를 프로젝트에 딱 한 번 넣는 파일. Model.cpp 가 GLB 안의 PNG·JPG 텍스처를 읽을 때 쓴다.
#include "pch.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996 4244 4456 4457 4701 4703)
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif
