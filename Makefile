override name := ft_ping

override src_dir := src

override srcs := main.c

# add prefix to srcs
override srcs := $(addprefix $(src_dir)/, $(srcs))

override objs := $(srcs:%.c=%.o) 

override deps := $(srcs:%.c=%.d)

def :=

override cflags := -Wall -Wextra -Werror -Wpedantic -g -O0 -I$(src_dir) $(def)

override depflags = -MT $@ -MMD -MF $(src_dir)/$*.d

override ldflags :=

.PHONY: all clean fclean re

all: $(name)

$(name): $(objs)
	gcc $^ -o $(name) $(ldflags)

-include $(deps)
src/%.o: src/%.c Makefile
	gcc $(cflags) $(depflags) -c $< -o $@ 

clean:
	@rm -vf $(objs) $(deps)

fclean: clean
	@rm -vf $(name) 

re: fclean all
