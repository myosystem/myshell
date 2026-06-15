#include <myos>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
int main() {
	Window* window = new Window({ 10,10,100,100 });
	for (uint32_t y = 0; y < window->rect.height; y++) {
		for (uint32_t x = 0; x < window->rect.width; x++) {
			((uint32_t*)window->gbuf)[y * window->rect.width + x] = 0;
		}
	}
	while (1) {
		char text[100];
		scanf("%s", text);
		printf("%s", text);
	}
}