# include <stdlib.h>
# include <stdio.h>
# include <stdbool.h>	// Містить чотири макроси для роботи з типом даних bool
# include <errno.h>		// Містить оголошення макросів для ідентифікації помилок через їх код
# include <dirent.h>	// Бібліотека для роботи з форматом запису каталогу
# include <string.h>
# include <time.h>
# include <sys/stat.h>	// Інформація про файли (stat() )

# define LISTFS_MAGIC 0x84837376	// Сигнатура файлової системи

# define LISTFS_VERSION 0x0001

// Створення нових імен для існуючих типів даних
// (це сприяє легшому створенню машинно-незалежних команд)
typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef signed short int16;
typedef unsigned int uint32;
typedef signed int int32;
typedef unsigned long long uint64;
typedef signed long long int64;


typedef struct
{
	char jump[4];
	uint32 magiс;		// Сигнатура файлової системи
	uint32 version;		// Версія файлової системи
	uint32 flags;		// Атрибути файлової системи
	uint64 base;		// Номер початкового сектора файлової системи
	uint64 size;		// Розмір файлової системи
	uint64 map_base;	// Базова адреса бітової карти вільних секторів
	uint64 map_size;	// Розмір бітової карти вільних секторів
	uint64 first_file;	// Номер сектора, що містить заголовок першого файлу у корені
	uint64 uid;			// Унікальний ідектифікатор файлової системи
	uint64 block_size;	// Розмір сектора. Слугує для підтрими будь-яких блочних пристроїв
} __attributе((packed)) listfs_header; // тепер це ім'я нового типу 
									   // Заголовок файлової системи

typedef struct
{
	char name[256];		// Ім'я файлу в кодуванні UTF-8
	uint64 next;		// Номер сектора із наступним заголовком файлу
	uint64 prev;		// Номер сектора із попереднім заголовком файлу
	uint64 parent;		// Вказівник на батьківський каталог
	uint64 flags;		// Атрибути файлу.
	uint64 data;		// Вказівник на дані файли.
	uint64 size;		// Розмір файлу в байтах
	uint64 create_time;	// Дата створення
	uint64 modify_time;	// Дата останньої зміни
	uint64 access_time;	// Дата останнього звернення
}__attributе((packed)) listfs_file_header;	// тепер це ім'я нового типу 
											// Заголовок файлу

char *output_file_name = NULL;		// Ім'я вихідного файлу
char *boot_loader_file_name = NULL;	// Ім'я файлу із завантажувачем
char *source_dir_name = NULL;		// Ім'я файлу джерела
long block_size = 512;				// Розмір сектору
long block_count = 0;				// Кількість секторів
FILE *output_file;					// Вказівник на вихідний файл
listfs_header *fs_header;			// Вказівник на структуру з даними про файлову систему
char *disk_map;						// Вказівник на дискову бітову карту 
long boot_loader_extra_blocks = 0;	// Додаткові блоки завантажувача

// Функція яка отримує командний рядок
bool get_commandline_options(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++)
	{
		if (!strncmp(argv[i], "of=", 3))
			// Отримали ім'я вихідного файлу
			output_file_name = argv[i] + 3;
		else if (!strncmp(argv[i], "bs=", 3))
			// Отримали розмір секторів
			block_size = atoi(argv[i] + 3);
		else if (!strncmp(argv[i], "size=", 5))
			// Отримали кількість секторів
			block_count = atoi(argv[i] + 5);
		else if (!strncmp(argv[i], "boot=", 5))
			// Отримали ім'я файлу завантажувача
			boot_loader_file_name = argv[i] + 5;
		else if (!strncmp(argv[i], "src=", 4))
			// Отримали ім'я файлу джерела
			source_dir_name = argv[i] + 4;
	}
	return (output_file_name && block_count);
}

// Виведення параметрів командного рядка
void usage()
{
	printf("Syntax:\n");
	printf(" make_listfs [options]\n");
	printf("Options:\n");
	printf("  of=XXX - output file name (required)\n");
	printf("  bs=XXX - size of disk image block in bytes (optional, default - 512)\n");
	printf("  size=XXX - size of boot loader image file (required)\n");
	printf("  boot=XXX - name of boot loader image file (optional, need only for bootable image)\n");
	printf("  src=XXX - dir, that contents will be copied on created disk image (optional)\n");
	printf("\n");
}

// Перевірка параметрів
bool check_options()
{
	// Якщо розмір сектора менший за 512 та не кратний 8
	if ((block_size < 512) && !(block_size & 7))
	{
		fprintf(stderr, "Error: Block size must be larger than 512 and be multiple of 8!\n");
		return false;
	}
	// Якщо кількість секторів менша за 2
	if (block_count < 2)
	{
		fprintf(stderr, "Error: Block count must be larger than 1!\n");
		return false;
	}
	return true;
}

// Відкрити вихідний файл
bool open_output_file()
{
	char *buf;		// Вказівник на символьний буфер
	output_file = fopen(output_file_name, "wb+");	// Створити вихідний двійковий файл для запису/читання
	if (!output_file)	// Якщо файл не вдалося створити
	{
		// Виведення трансляції коду помилки
		// strerror() - транслює код помилки, що розміщений у глобальній змінній errno
		fprintf(stderr, "Error: Could not create output file: %s\n", strerror(errno));
		return false;
	}
	// Зміна вказівника положення у файлі output_file, в положення block_size * (block_count-1) від початку файлу (SEEK_SET)
	fseek(output_file, block_size * (block_count - 1), SEEK_SET);
	// Виділення пам'яті для масиву в 1 комірку розміром block_size
	buf = (char *)calloc(1, block_size);
	// Записуємо 1 об'єкт розміром block_size у output_file із символьного масиву buf
	if (fwrite(buf, block_size, 1, output_file) == 0)	// Якщо не вдалося записати
	{
		// Виведення трансляції коду помилки
		// strerror() - транслює код помилки, що розміщений у глобальній змінній errno
		fprintf(stderr, "Error: Could not create output file: %s\n", strerror(errno));
		fclose(output_file);		// Закриваємо вихідний файл
		remove(output_file_name);	// Видаляємо вихідний фалй
		free(buf);	// Звільнюємо пам'ять, зайняту буфером buf
		return false;
	}
	free(buf);	// Звільнюємо пам'ять, зайняту буфером buf
	return true;
}

// Функція для закриття вихідного файлу
void close_output_file()
{
	fclose(output_file);
}

// write_block: функція запису блоку даних у вихідний файл
// index - положення даних
// data - вказівник на дані
// count - кількість об'єктів для запису
void write_block(uint64 index, void *data, unsigned int count)
{
	// Зміна вказівника положення у файлі output_file, в положення block_size * index, від початку файлу (SEEK_SET)
	fseek(output_file, block_size * index, SEEK_SET);
	// Записуємо count об'єктів розміром block_size у output_file із ділянки за адресою data
	fwrite(data, block_size, count, output_file);
}
// init_listfs_header: функція ініціалізації заголовка файлової системи
void init_listfs_header()
{
	
	//***********************************************************
	fs_header = calloc(block_size, 1);	// Виділення пам'яті для масиву в 512 комірок розміром 1
	//***********************************************************
	if (boot_loader_file_name)	// Якщо задане ім'я файлу завантажувача
	{
		FILE *f;				// Файловий вказівник
		f = fopen(boot_loader_file_name, "rb");	// Відкриємо двійковий файл для читання
		if (f)	// Якщо файл відкрився
		{
			// Прочитаємо 1 об'єт розміром block_size із f у структуру на яку вказує fs_header
			fread(fs_header, block_size, 1, f);
			if (!feof(f)) // Якщо нам не зустрівся кінець файлу
			{
				char *buffer = (char *)calloc(block_size, 1); // Виділення пам'яті для масиву в 512 комірок розміром 1
				int i = 1; // індекс положення даних
				while (!feof(f)) // Доки не зустрінеться кінець файлу
				{
					// Прочитаємо 1 об'єт розміром block_size із f у буфер buffer
					fread(buffer, block_size, 1, f);
					// Запишемо блок даних з buffer кількістю 1 в позицію i
					write_block(i, buffer, 1);
					i++; // Збільшимо індекс положення даних
					boot_loader_extra_blocks++; // Збільшим кількість додаткових блоків завантажувача
				}
			}
			fclose(f); // Закриємо файл, на який вказує f
		}
		else
			fprintf(stderr, "Warning: Could not open boot loader file for reading. No boot loader will be installed.\n");
	}
	fs_header->magic = LISTFS_MAGIC;	// Сигнатура файлової системи
	fs_header->version = LISTFS_VERSION;// Версія файлової системи
	fs_header->base = 0;				// Номер початкового сектора 
	fs_header->size = block_count;		// Кількість секторів (розмір файлової системи)
	fs_header->first_file = -1;		// -1 - означає, що диск порожність
	fs_header->block_size = block_size; // Розмір сектора
	fs_header->uid = (time(NULL) << 16) | (rand() & 0xFFFF); // Унікальний ідентифікатор файлової системи
}

// store_listfs_header: функція запису заголовка файлової системи
// fs_header у вихідний файл output_file
void store_listfs_header()
{
	// Запишемо 1 ділянку розміром block_size
	// з fs_header у вихідний файл по зміщенню 0
	write_block(0, fs_header, 1);
	free(fs_header);	// Звільнимо пам'ять зайняту заголовком файлової системи
}

// alloc_disk_block: рахує скільки блоків займає диск 
uint64 alloc_disk_block()
{
	// Починаємо з індексу зміщення 0
	uint64 index = 0;	

	// доки ділянка бітової карти диску по індексу index /= 8 (index >> 3)
	// та значення 1 * pow(2, index & 7), де (index & 7) - остача від ділення індексу на 8
	// є істинними
	while (disk_map[index >> 3] & (1 << (index & 7)))
	{
		index++;  // збільшуємо індекс
		if (index >= fs_header->size)	// якщо індекс більший/рівний розміру файлової системи
		{
			fprintf(stderr, "Error: Disk image is full. Could not write more.\n");
			exit(-3);
		}
	}
	disk_map[index >> 3] |= 1 << (index & 7);
	return index;
}

// Беремо значення з бітової карти диску disk_map по індексу index
void get_disk_block(uint64 index)
{
	disk_map[index >> 3] |= (index & 7);
}

// init_disk_map: ініціалізація дискової бітової карти disk_map
void init_disk_map()
{
	int i;
	// Номер базової адреси бітової карти вільних секторів ініціалізуємо
	// значенням кількості секторів, які займає завантажувач
	fs_header->map_base = /* 1+ */ boot_loader_extra_blocks;
	// Розмір бітової карти вільних секторів дорівнює
	// кількості блоків (розмір файлової системи), що поділена на 8
	fs_header->map_size = block_count / 8;
	// Якщо розмір бітової карти вільних секторів не є кратним 
	// розміру сектора, то збільшуємо розмір бітової карти на розмір сектора
	if (fs_header->map_size % block_size) fs_header->map_size += block_size;
	// Визначаємо скільки секторів (блоків) займає бітова карта вільних секторів
	fs_header->map_size /= block_size;
	// Виділяємо пам'ять для масиву бітової карти вільних секторів disk_map
	// розміром block_size = 512 байт кожен блок
	// та кількістю fs_header->map_size блоків
	disk_map = (char *) calloc(block_size, fs_header->map_size);

	// Проходимо по кожному індексу (блоку) бітової карти вільних секторів
	for (i = 0; i < fs_header->map_base + fs_header->map_size; i++)
	{
		get_disk_block(i);
	}
}

// store_file_data: запишемо дані файлу file у вихідний файл output_file
// та повернемо індекс на кінцевий блок
uint64 store_file_data(FILE *file)
{
	// Якщо дійшли до кінця файлу
	if (feof(file)) return -1;
	// Виділимо ділянку пам'яті розміром в 1 блок (сектор = 512 байт) для сектора зі списком секторів файлів
	uint64 *block_list = (uint64 *) malloc(block_size);
	// Створимо змінну, що означатиме індекс у списку секторів файлу
	uint64 block_list_index = alloc_disk_block();
	// Виділимо ділянку в 512 байт для даних що містяться у секторі
	char *block_data = (char *) malloc(block_size);
	int i;
	// (block_size / 8 - 1) - це кількість секторів у списку
	for (i = 0; i < (block_size / 8 - 1); i++)
	{
		if (!feof(file))
		{
			// Записуємо нулями (2-й аргумент)
			// перші 512 позицій (block_size)
			// у block_data 
			memset(block_data, 0, block_size);
			// Якщо не прочитаються дані з file у block_data
			if (fread(block_data, 1, block_size, file) == 0)
			{
				// Наш сектор є порожнім, тому записуємо -1
				block_list[i] = -1;
				continue;
			}
			// Індекс максимального блоку
			uint64 block_index = alloc_disk_block();
			// Записуємо у поточний блок індекс максимального блоку
			block_list[i] = block_index;
			// Записуємо дані block_data у вихідний файл по ідексу block_index кількістю один об'єкт
			write_block(block_index, block_data, 1);
		}
		// Інакше ми дійшли до кінця файлу
		else
		{
			// і запишемо в кінець сектора зі списком секторів файлу -1, що є ознакою кінця
			block_list[i] = -1;
		}
	}
	// 
	block_list[block_size / 8 - 1] = store_file_data(file);
	// Запишемо у вихідний файл output_file по індексу block_list_index дані з block_list
	write_block(block_list_index, block_list, 1);
	// Звільнимо пам'ять
	free(block_list);
	free(block_data);

	// Повернемо індекс останнього блоку
	return block_list_index;
}

// Читає список файлів у даному каталозі dir_name
uint64 process_dir(uint64 parent, char *dir_name)
{
	DIR *dir = opendir(dir_name);	// Відкриємо каталог
	struct dirent *entry;	// Вказівник на дані про файли в директорії
	uint64 prev_file = -1;	// Попередній файл
	uint64 cur_file;		// Поточний файл
	uint64 first_file = -1;	// Перший файл
	char file_name[260];
	struct stat file_stat;	// Структура, що міститиме результат функції stat()
	listfs_file_header *file_header;	// Заголовок файлу
	if (!dir) // Якщо каталог не вдалося відкрити
	{
		fprintf(stderr, "Warning: could not read contents of directory '%s': %s\n", dir_name, strerror(errno));
		return -1;
	}
	// Виділимо пам'ять для файлового заголовку
	file_header = (listfs_file_header *) malloc(block_size);
	while (entry = readdir(dir))	// Доки у директорії читаються (існують) файли
	{
		// Якщо прочитали імена поточного або попереднього каталогу
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
		// Поточний файл має кінцевий індекс
		cur_file = alloc_disk_block();
		// Якщо файл не перший у каталозі
		if (prev_file != -1)
		{
			// Номер сектора наступного файлу дорівнює поточному
			file_header->next = cur_file;
			// Запишемо у вихідний файл дані з file_header по індексу prev_file
			write_block(prev_file, file_header, 1);
		}
		else
			first_file = cur_file;
		// Запишемо нулями сектор file_header
		memset(file_header, 0, block_size);
		file_header->prev = prev_file;	// Номер сектора з попереднім заголовком файлу дорівнює prev_file
		file_header->next = -1;			// Номер сектора з наступним заголовком файлу дорівнює -1, тобто поточний файл є останнім
		// Запишемо у file_header->name ім'я файлу з entry->d_name
		strncpy(file_header->name, entry->d_name, sizeof(file_header->name) - 1);
		file_header->name[sizeof(file_header->name) - 1] = '\0'; // Ознака кінця імені
		file_header->size = 0; // Розмір файлу у байтах
		sprintf((char*)&file_name, "%s/%s", dir_name, entry->d_name);
		// Функція stat() отримує інформацію про файл file_name та записує її у file_stat
		stat(file_name, &file_stat);
		file_header->parent = parent;	// Батьківський каталог
		file_header->flags = 0;			// Атрибут файлу. 0 - ознака файлу
		file_header->create_time = file_stat.st_ctime;	// Час створення
		file_header->modify_time = file_stat.st_mtime;	// Час останньої модифікації
		file_header->access_time = file_stat.st_atime;	// Час останнього звернення
		// Якщо це каталог
		if (S_ISDIR(file_stat.st_mode))
		{
			file_header->flags = 1;	// Це каталог
			file_header->data = process_dir(cur_file, file_name);
		}
		// Якщо це файл
		else if (S_ISREG(file_stat.st_mode))
		{
			// Відкриємо бінарний файл file_name для читання
			FILE *f = fopen(file_name, "rb");
			if (f)	// Якщо файл відкрився
			{
				file_header->size = file_stat.st_size;	// Розмір файлу
				file_header->data = store_file_data(f);	// Вказівник на дані файли
				fclose(f);	// Закриємо файл
			}
			else
			{
				// Неможливо відкрити файл для читання
				fprintf(stderr, "Warning: Could not open file '%s' for reading!\n", file_name);
				file_header->data = -1; // Наш каталог є порожнім
			}
		}
		else
		{
			// Даний файл не є директорією або звичайним файлом
			fprintf(stderr, "Warning: File '%s' is not directory or regular file.\n", file_name);
		}
		// Попередній файл стає поточним
		prev_file = cur_file;
	}
	// Запишемо дані file_header у вихідний файл output_file
	// по індексу розміщення prev_file
	write_block(prev_file, file_header, 1);
	free(file_header);	// Звільнимо пам'ять
	closedir(dir);		// Закриємо каталог
	return first_file;
}


int main(int argc, char *argv[])
{
	// Якщо не отримано параметри командного рядка
	if (!get_commandline_options(argc, argv))
	{
		// Виведемо характеристики параметрів, що вимагаються
		usage();
		return 0;
	}
	if (!check_options()) return -1;		// Якщо отримані параметри командного рядка не є валідними
	if (!open_output_file()) return -2;		// Створимо вихідний файл
	init_listfs_header();	// Ініціалізуємо заголовок файлової системи
	init_disk_map();		// Ініціалізуємо бітову карту диска
	store_listfs_header();	// Запишемо заголовок файлової системи у вихідний файл
	close_output_file();	// Закриємо вихідний файл

	return 0;
}










