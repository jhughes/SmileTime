list * list_find(list * head, char * name){
	printf("%d\n", head);
	while(head != NULL){
		if( strcmp(head->name, name) == 0){
			return head;
		}
		head = head->next;
	}
	return "NULL";
}


/*
char* strstp(char * str, char * stp, int * size){
	char * loc = strstr(str, stp);
	*size = loc-str+1;
	char * retstr = malloc(*size);
	memset(retstr, 0, *size);
	strncpy(retstr, str, *size-1);
	retstr[*size-1] = '\0';
	return retstr;
}*/
void list_add(list ** head, char * name, char * ip, char * port, char protocol){
	if(*head == NULL){
		list * newnode = malloc(sizeof(list));
		newnode->name = name;
		newnode->ip = ip;
		newnode->port = port;
		newnode->protocol = protocol;
		newnode->next = NULL;
		*head = newnode;
		return;
	}
	list * curr = *head;

	while(curr->next != NULL){
		// Increment to the end of the list
		if( strcmp(curr->name, name) == 0)
			return; // Already exists
		curr = curr->next;
	}
	curr->next = malloc(sizeof(list));
	curr = curr->next;
	curr->name = name;
	curr->port = port;
	curr->ip = ip;
	curr->protocol = protocol;
	curr->next = NULL;
}

int list_length(list * head){
	int count = 0;
	while( head != NULL){
		count++;
		head = head->next;
	}
	return count;
}

void list_destroy(list ** head){
	if(head == NULL || *head == NULL){
		return;
	}
	list_destroy(&((*head)->next));
	free(*head);
	*head = NULL;
}

void list_print(list * head){
	int count = 0;
	while(head != NULL){
		printf("%d> %s - %s:%s [%c]\n", count, head->name, head->ip, head->port,head->protocol);
		count++;
		head = head->next;
	}
}
