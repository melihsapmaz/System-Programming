#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

//Student struct to store student name, surname, grade and the line from the file during sorting
typedef struct {
	char *name;
	char *surname;
	char *grade;
	char *line;
} Student;

//Function to remove a character from a string
char *removeCharacter(char *str, char ch) {
	int i, j = 0, len;
	char *output = NULL;
	len = strlen(str);
	output = (char *) malloc(sizeof(char) * (len + 1));

	for (i = 0; i < len; i++) {
		if (str[i] != ch) {
			output[j] = str[i];
			j++;
		}
	}
	output[j] = '\0';
	return output;
}

//Comparison functions for qsort
int compareByNameAscending(const void *a, const void *b) {
	Student *student1 = (Student *) a;
	Student *student2 = (Student *) b;
	return strcmp(student1->name, student2->name);
}

int compareByGradeAscending(const void *a, const void *b) {
	Student *student1 = (Student *) a;
	Student *student2 = (Student *) b;
	return strcmp(student2->grade, student1->grade);
}

int compareByNameDescending(const void *a, const void *b) {
	Student *student1 = (Student *) a;
	Student *student2 = (Student *) b;
	return strcmp(student2->name, student1->name);
}

int compareByGradeDescending(const void *a, const void *b) {
	Student *student1 = (Student *) a;
	Student *student2 = (Student *) b;
	return strcmp(student1->grade, student2->grade);
}

void printStudents(Student *students, int size) {
	for (int i = 0; i < size; i++) {
		printf("%s\n", students[i].line);
	}
}

//Function to sort the students array
void sortStudents(Student *students, size_t size, int option) {
	//Create a temporary array of students to sort
	Student *temp = (Student *) malloc(size * sizeof(Student));
	//Copy the students array to the temporary array
	for (int i = 0; i < (int)size; i++) {
		char *lineCopy = strdup(students[i].line);
		temp[i].name = strtok(lineCopy, " ");
		temp[i].surname = strtok(NULL, " ");
		temp[i].grade = strtok(NULL, " ");
		temp[i].line = strdup(students[i].line);
	}

	//Sort the temporary array of students based on the option using qsort
	if (option == 1) {
		qsort(temp, size, sizeof(Student), compareByNameAscending);
	}
	else if (option == 2) {
		qsort(temp, size, sizeof(Student), compareByGradeAscending);
	}
	else if (option == 3) {
		qsort(temp, size, sizeof(Student), compareByNameDescending);
	}
	else if (option == 4) {
		qsort(temp, size, sizeof(Student), compareByGradeDescending);
	}
	//Print the sorted students
	printStudents(temp, size);
	//Free the temporary array
	free(temp);
}

//Function to sort the file based on the option
int sortFile(char *filename, int option) {
	char buffer[100];
	char ch;
	Student students[100];
	int i = 0;
	int lineCount = 0;

	//Open the file
	int file = open(filename, O_RDONLY);
	if (file == -1) {
		perror("File Open Failed\n");
		return -1;
	}
	//Read the file character by character and store the lines in the students array
	while (read(file, &ch, 1) > 0) {
		//If a newline character is encountered, store the line in the students array
		if (ch == '\n') {
			buffer[i] = '\0'; //Null terminate the buffer
			students[lineCount].line = (char *) malloc(strlen(buffer) * sizeof(char)); 
			strcpy(students[lineCount].line, buffer);						//Copy the line to the students array
			students[lineCount].name = strtok(buffer, " ");					//Tokenize the line and store the name, surname and grade in the students array
			char *temp = removeCharacter(students[lineCount].name, '\"');	//Remove the double quotes from the name
			students[lineCount].name = temp; 								//Store the name in the students array
			students[lineCount].surname = strtok(NULL, " ");				//Tokenize the line and store the surname in the students array
			temp = removeCharacter(students[lineCount].surname, ',');		//Remove the comma from the surname
			students[lineCount].surname = temp;								//Store the surname in the students array
			free(temp);														//Free the temporary string
			students[lineCount].grade = strtok(NULL, " ");					//Tokenize the line and store the grade in the students array
			i = 0;															//Reset the buffer index
			lineCount++;													//Increment the line count
		}
		//If a character is not a newline character, store it in the buffer
		else {
			buffer[i] = ch;		//Store the character in the buffer
			i++;				//Increment the buffer index
		}
	}
	//If the buffer is not empty, store the last line in the students array
	if (i != 0) {
		buffer[i] = '\0';
		students[lineCount].line = (char *) malloc(strlen(buffer) * sizeof(char));
		strcpy(students[lineCount].line, buffer);
		students[lineCount].name = strtok(buffer, " ");
		students[lineCount].surname = strtok(NULL, " ");
		students[lineCount].grade = strtok(NULL, " ");
	}

	close(file);	//Close the file
	sortStudents(students, lineCount, option);	//Sort the students array based on the option
	//Free the memory allocated for the lines
	for (int i = 0; i < lineCount; i++) {
		free(students[i].line);
	}
	return 0;
}

//Function to write to the log file
void logFileWrite(char *logFile, char *message) {
	pid_t pid;
	int status;
	time_t currentTime;
	pid = fork();																			//Fork a child process
	if (pid == 0) {																			//If the process is the child
		int logFileDescriptor = open(logFile, O_CREAT | O_WRONLY | O_APPEND, 0777);			//Open the log file
		if (logFileDescriptor == -1) {														//If the file open fails, print an error message and exit
			perror("Log File Open Failed\n");												//Print an error message
			_exit(EXIT_FAILURE);															//Exit the child process
		}
		time(&currentTime);																//Get the current time
		char *timeString = ctime(&currentTime);											//Convert the time to a string
		write(logFileDescriptor, timeString, strlen(timeString));						//Write the time to the log file
		write(logFileDescriptor, message, strlen(message));								//Write the message to the log file
		close(logFileDescriptor);														//Close the log file
		_exit(EXIT_SUCCESS);															//Exit the child process
	}
	else if (pid > 0) {		//If the process is the parent
		wait(&status);		//Wait for the child process to exit
		kill(pid, SIGKILL);	//Kill the child process
	}
	else {
		printf("Fork Failed During Log File Write\n");	//Print an error message if the fork fails
	}
}

int main() {
	char *logFile = "log.txt";	//Log file name
	char command[100];
	char *args[5] = { NULL }; 	//Array to store the command and its arguments
	char *token = NULL;
	int i;					//Index for the args array
	pid_t pid;				//Process ID
	int file;				//File descriptor
	int status;				//Status of the child process

	while (1) {
		printf("Enter a command: ");
		fgets(command, 100, stdin);	//Read the command from the user
		token = strtok(command, " ");	//Tokenize the command
		i = 0;						//Reset the index
		while (token != NULL) {		//Store the tokens in the args array
			args[i] = token;
			int len = strlen(args[i]);	//Remove the newline character from the end of the token
			if (len > 0 && args[i][len - 1] == '\n') {
				args[i][len - 1] = '\0';
			}
			token = strtok(NULL, " ");	//Get the next token
			i++;					//Increment the index
		}
		if (strcmp(args[0], "gtuStudentGrades") == 0) {				//If the command is gtuStudentGrades
			if (args[1] == NULL) {									//If the file name is not provided
				printf("Create an Empty File                      => gtuStudentGrades filename.txt\n");
				printf("Append Student Name and Grade to the file => addStudentGrade Name Grade filename.txt\n");
				printf("Search Student Name Surname Grade         => searchStudent Name filename.txt\n");
				printf("Sort All Entries                          => sortAll filename.txt\n");
				printf("Show All Entries                          => showAll filename.txt\n");
				printf("List First 5 Entries                      => listGrades filename.txt\n");
				printf("List Some Entries                         => listSome numOfEntries pageNumber filename.txt\n");
			}
			else {					//If the file name is provided
				pid = fork();		//Fork a child process
				if (pid == 0) {		//If the process is the child
					file = open(args[1], O_CREAT | O_RDWR, 0777);	//Create the file if it does not exist and open it for reading and writing with read and write permissions for all users
					if (file == -1) {								//If the file open fails, print an error message and exit
						_exit(EXIT_FAILURE);						//Exit the child process with a failure status
					}
					close(file);									//Close the file
					_exit(EXIT_SUCCESS);							//Exit the child process with a success status
				}
				else if (pid > 0) {									//If the process is the parent
					wait(&status);									//Wait for the child process to exit
					if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE) {		//If the child process exits with a failure status
						char *message = " File Creation Failed During Creating an Empty File.\n";	
						logFileWrite(logFile, message);				//Write an error message to the log file
					}
					else {
						char *message = " File Created Successfully.\n";
						logFileWrite(logFile, message);				//Write a success message to the log file
					}
					kill(pid, SIGKILL);								//Kill the child process
				}
				else {
					char *message = " Fork Failed During Creating an Empty File.\n";	
					logFileWrite(logFile, message);			//Write an error message to the log file
				}
			}
		}
		else if (strcmp(args[0], "addStudentGrade") == 0) {		//If the command is addStudentGrade
			if (args[1] == NULL || args[2] == NULL || args[3] == NULL || args[4] == NULL) {	//If the name, surname, grade or file name is not provided
				printf("Usage: addStudentGrade Name Surname Grade filename.txt\n");	//Print the usage message
			}
			else {
				pid = fork();
				if (pid == 0) {
					file = open(args[4], O_WRONLY | O_APPEND);	//Open the file for writing and appending to the end of the file
					if (file == -1) {
						_exit(EXIT_FAILURE);
					}
					write(file, "\"", 1);	//Write the name, surname and grade to the file with this format: "Name Surname, Grade"
					write(file, args[1], strlen(args[1]));
					write(file, " ", 1);
					write(file, args[2], strlen(args[2]));
					write(file, ",", 1);
					write(file, " ", 1);
					write(file, args[3], strlen(args[3]));
					write(file, "\"", 1);
					write(file, "\n", 1);

					close(file);
					_exit(EXIT_SUCCESS);
				}
				else if (pid > 0) {
					wait(&status);
					if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE) {
						char *message = " File Open Failed During Adding Student Grade.\n";
						logFileWrite(logFile, message);
					}
					else {
						char *message = " Student Grade Added Successfully.\n";
						logFileWrite(logFile, message);
					}
					kill(pid, SIGKILL);
				}
				else {
					char *message = " Fork Failed During Adding Student Grade.\n";
					logFileWrite(logFile, message);
				}
			}
		}
		else if (strcmp(args[0], "searchStudent") == 0) {				//If the command is searchStudent
			if (args[1] == NULL || args[2] == NULL || args[3] == NULL) {
				printf("Usage: searchStudent Name Surname filename.txt\n");
			}
			else {
				pid = fork();
				if (pid == 0) {
					file = open(args[3], O_RDONLY);
					if (file == -1) {
						_exit(EXIT_FAILURE);
					}
					char buffer[100];
					int found = 0;
					char ch;
					int j = 0;
					while (read(file, &ch, 1) > 0) {		//Read the file character by character
						if (ch != '\n' && ch != EOF) {
							buffer[j] = ch;
							j++;
						}
						else {
							buffer[j] = '\0';
							if (strstr(buffer, args[1]) != NULL && strstr(buffer, args[2]) != NULL) { 		//If the name and surname are found in the line
								printf("%s\n", buffer);														//Print the line
								found = 1;																	//Set the found flag to 1
								break;
							}
							//clear buffer
							memset(buffer, 0, sizeof(buffer));												//Clear the buffer
							j = 0;
						}
					}
					if (found == 0) {								//If the student is not found
						char *message = " Student Not Found.\n";
						logFileWrite(logFile, message);				//Write an error message to the log file
					}
					close(file);
					_exit(EXIT_SUCCESS);
				}
				else if (pid > 0) {
					wait(&status);
					if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE) {
						char *message = " File Open Failed During Searching Student.\n";
						logFileWrite(logFile, message);
					}
					else {
						char *message = " File Opened Successfully.\n";
						logFileWrite(logFile, message);
					}
					kill(pid, SIGKILL);
				}
				else {
					char *message = " Fork Failed During Searching Student.\n";
					logFileWrite(logFile, message);
				}
			}
		}
		else if (strcmp(args[0], "sortAll") == 0) {				//If the command is sortAll
			if (args[1] == NULL) {
				printf("Usage: sortAll filename.txt\n");
			}
			else {
				pid = fork();
				if (pid == 0) {
					printf("Enter an option to sort the file by name or grade\n");	//Ask the user to enter an option to sort the file
					printf("1: Sort by Name Ascending\n");
					printf("2: Sort by Grade Ascending\n");
					printf("3: Sort by Name Descending\n");
					printf("4: Sort by Grade Descending\n");
					int option;
					scanf("%d", &option);											//Read the option from the user
					if (option != 1 && option != 2 && option != 3 && option != 4) {	//If the option is not valid, print an error message and exit
						char *message = " Invalid Option\n";
						logFileWrite(logFile, message);
						_exit(EXIT_FAILURE);
					}
					if (sortFile(args[1], option) == -1) {							//Sort the file based on the option and print an error message if the file open fails
						char *message = " File Sorted Successfully.\n";
						logFileWrite(logFile, message);
						_exit(EXIT_FAILURE);
					}
					else {
						char *message = " File Not Sorted Successfully.\n";			//Print a success message if the file is not sorted successfully
						logFileWrite(logFile, message);
						_exit(EXIT_SUCCESS);
					}
				}
				else if (pid > 0) {												//If the process is the parent
					wait(&status);
					kill(pid, SIGKILL);
				}
				else {														//If the fork fails
					char *message = " Fork Failed During Sorting File.\n";
					logFileWrite(logFile, message);
				}
			}
		}
		else if (strcmp(args[0], "showAll") == 0) {			//If the command is showAll
			if (args[1] == NULL) {
				printf("Usage: showAll filename.txt\n");
			}
			else {
				pid = fork();
				if (pid == 0) {
					file = open(args[1], O_RDONLY);			//Open the file for reading
					if (file == -1) {
						_exit(EXIT_FAILURE);
					}
					char buffer[100];
					int n;
					while ((n = read(file, buffer, 100)) > 0) {	//Read the file and print the contents to the console until the end of the file
						if (n == -1) {
							_exit(EXIT_FAILURE);
						}
						buffer[n] = '\0';
						printf("%s", buffer);
					}
					close(file);
					_exit(EXIT_SUCCESS);
				}
				else if (pid > 0) {
					wait(&status);
					if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE) {
						char *message = " File Open Failed During Showing All Entries.\n";
						logFileWrite(logFile, message);
					}
					else {
						char *message = " File Opened Successfully During Showing All Entries.\n";
						logFileWrite(logFile, message);
					}
					kill(pid, SIGKILL);
				}
				else {
					char *message = " Fork Failed During Showing All Entries.\n";
					logFileWrite(logFile, message);
				}
			}
		}
		else if (strcmp(args[0], "listGrades") == 0) {			//If the command is listGrades
			if (args[1] == NULL) {
				printf("Usage: listGrades filename.txt\n");
			}
			else {
				pid = fork();
				if (pid == 0) {
					file = open(args[1], O_RDONLY);
					if (file == -1) {
						_exit(EXIT_FAILURE);
					}
					char buffer[100];
					int j = 0;
					char ch;
					int bufferIndex;
					//read one character at a time until a newline character is encountered and store it in buffer and only read 5 lines
					while (j < 5) {
						bufferIndex = 0;
						while (read(file, &ch, 1) > 0) {
							if (ch == '\n') {
								buffer[bufferIndex] = '\0';
								printf("%s\n", buffer);
								j++;
								break;
							}
							else {
								buffer[bufferIndex] = ch;
								bufferIndex++;
							}
						}
					}


					close(file);
					_exit(EXIT_SUCCESS);
				}
				else if (pid > 0) {
					wait(&status);
					if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE) {
						char *message = " Entries Listing Failed\n";
						logFileWrite(logFile, message);
					}
					else {
						char *message = " Entries Listed Successfully.\n";
						logFileWrite(logFile, message);
					}
					kill(pid, SIGKILL);
				}
				else {
					char *message = " Fork Failed During Listing Entries.\n";
					logFileWrite(logFile, message);
				}
			}
		}
		else if (strcmp(args[0], "listSome") == 0) {					//If the command is listSome
			if (args[1] == NULL || args[2] == NULL || args[3] == NULL) {
				printf("Usage: listSome numOfEntries pageNumber filename.txt\n");
			}
			else {
				pid = fork();
				if (pid == 0) {
					file = open(args[3], O_RDONLY);
					if (file == -1) {
						_exit(EXIT_FAILURE);
					}
					char buffer[100];
					char ch;
					for (int i = 0; i < (atoi(args[2])) * atoi(args[1]); i++) {
						//read one character at a time until a newline character is encountered and store it in buffer
						if (i < (atoi(args[2]) - 1) * atoi(args[1])) {	//skip the lines until the start of the page
							while (read(file, &ch, 1) > 0) {
								if (ch == '\n') {
									break;
								}
							}
						}
						else {									//print the lines of the page
							int j = 0;
							while (read(file, &ch, 1) > 0) {		//read the file character by character
								if (ch == '\n') {
									buffer[j] = '\0';
									break;
								}
								else {
									buffer[j] = ch;
									j++;
								}
							}
							printf("%s\n", buffer);
						}
					}

					close(file);
					_exit(EXIT_SUCCESS);
				}
				else if (pid > 0) {
					wait(&status);
					if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE) {
						char *message = " Entries Listing Failed\n";
						logFileWrite(logFile, message);
					}
					else {
						char *message = " Entries Listed Successfully.\n";
						logFileWrite(logFile, message);
					}
					kill(pid, SIGKILL);
				}
				else {
					char *message = " Fork Failed During Listing Entries.\n";
					logFileWrite(logFile, message);
				}
			}
		}
		else if (strcmp(args[0], "exit") == 0) {		//If the command is exit
			char *message = " Exiting Program.\n";
			logFileWrite(logFile, message);				//Write a message to the log file
			break;
		}
		else {
			char *message = " Invalid Command.\n";			//Write an error message to the log file if the command is invalid
			logFileWrite(logFile, message);
		}
		for (int i = 0; i < 5; i++) {						//Reset the args array
			args[i] = NULL;
		}
	}

	return 0;
}