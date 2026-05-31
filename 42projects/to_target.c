/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   test.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: seunghan <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2024/01/02 15:51:35 by seunghan          #+#    #+#             */
/*   Updated: 2024/02/11 19:16:26 by seunghan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

void	get_src(int x, int y, int *div, int *mod, int *fair_db)
{
	if (x >= y)
	{
		*div = x / y;
		*mod = x % y;
		if (!(*mod))
			*fair_db = y;
		else
			*fair_db = y / (*mod);
	}
	else
	{
		*div = y / x;
		*mod = y % x;
		if (!(*mod))
			*fair_db = x;
		else
			*fair_db = x / (*mod);
	}
}

int	special_case(int x, int y, int div, int mod, int *fair_db)
{
	int last_db;
	int	last_db_mod;

	last_db = 0;
	if (x >= y)
	{
		if (y - (*fair_db) * mod > 3)
			(*fair_db)++;
		last_db_mod = x - (y * div) - (y / (*fair_db));
		if (last_db_mod != 0)
		{
			last_db = y / last_db_mod;
			if (y / last_db - last_db_mod  > 3)
				last_db++;
		}
	}
	else
	{
		if (x - (*fair_db) * mod > 3)
			(*fair_db)++;
		last_db_mod = y - (x * div) - (x / (*fair_db));
		if (last_db_mod != 0)
			last_db = x / last_db_mod;
	}
	return (last_db);
}

void	is_x_big(int x, int y, int div, int mod, int fair_db)
{
	int	i;
	int	j;
	int	fair_db_const;
	int	last_db;
	int	last_db_const;

	i = 0;
	j = 0;
	last_db = special_case(x, y, div, mod, &fair_db);
	fair_db_const = fair_db;
	last_db_const = last_db;
	while (j < y)
	{
		i += div;
		if (mod > 0 && fair_db == j)
		{
			i++;
			mod--;
		}
		if (mod > 0 && j == last_db && last_db != 0)
		{
			i++;
			mod--;
			last_db += last_db_const;
		}
		if (fair_db <= j)
			fair_db += fair_db_const;
		if (mod && j == y - 1)
			i = x;
		j++;
		printf("%d  %d\n", i, j);
	}
}

void	is_y_big(int x, int y, int div, int mod, int fair_db)
{
	int	i;
	int	j;
	int	fair_db_const;
	int	last_db;
	int	last_db_const;

	i = 0;
	j = 0;
	last_db = special_case(x, y, div, mod, &fair_db);
	fair_db_const = fair_db;
	last_db_const = last_db;
	while (j < x)
	{
		i += div;
		if (mod > 0 && fair_db == j)
		{
			i++;
			mod--;
		}
		if (mod > 0 && j == last_db && last_db != 0)
		{
			i++;
			mod--;
			last_db += last_db_const;
		}
		if (fair_db <= j)
			fair_db += fair_db_const;
		if (mod && j == x - 1)
			i = y;
		j++;
		printf("%d  %d\n", j, i);
	}
}

int main(void)
{
	int x = 10;
	int y = 1;
	int div;
	int	mod;
	int	fair_db;

	get_src(x, y, &div, &mod, &fair_db);
	if (x >= y)
		is_x_big(x, y, div, mod, fair_db);
	else
		is_y_big(x, y, div, mod, fair_db);
}
