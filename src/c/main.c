#include <pebble.h>

#define PERSIST_DATA_MAX_LENGTH 256
#define PERSIST_STRING_MAX_LENGTH PERSIST_DATA_MAX_LENGTH 

#define TASK_START_ID 1 
#define TASK_END_ID 49
#define MEASUREMENTS_START_ID 50
#define MEASUREMENTS_END_ID 238
#define INTERRUPTION_START_ID 239
#define INTERRUPTION_END_ID 240

/* this is the id where the real last measurement is stored, used to display the title */
#define LAST_MEASUREMENT_ID 256

/* this is the id where the id of the last measurement is stored, used to define the "enddatetime" when ending the tracking. */
#define LAST_MEASUREMENT_ID_ID 255

static char s_localtime_string[] = "00:00   "; // leave spaces for AM/PM

typedef enum {
  StartTracking,
  Interruption,
  EndTracking,
  ShowTasks,
  ShowMeasurements,
  SyncMeasurements,
  Clear
} TaskSelectionAction;

typedef struct {
  TaskSelectionAction action;
} Context;

typedef struct {
  int id;
  char* title;
} Task;

typedef struct {
  int taskId;
  long int datetime;
  long int endDatetime;
} Measurement;

#define KEY_BUTTON 0

bool LOGGING = false;

Window *window;

TextLayer *selectedTaskLayer;
TextLayer *selectedTimeLayer;
time_t selectionTime;

/** The menu to display all possible tasks (are part of the listTaskLayer). Asked from the android configuration. */
MenuLayer *taskMenuLayer;
//MenuLayer *measurementsMenuLayer;

Task selectedTaskItem;

ActionMenu *taskSelectionActionMenu;
ActionMenuLevel *rootMenuLevel, *customMenuLevel;

bool selected = false;

//methods
static void showText(const char* text);
static void addTask(uint32_t index, char *title);
//static void initActionMenu();
static void action_performed_callback(ActionMenu *actionMenu, const ActionMenuItem *action, void* context);
static void endTracking();

static Task loadLastMeasurementTask();

//------------------------------- TASK MENU ----------------------------------------------------------------

static uint16_t task_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  uint32_t key = TASK_START_ID;
  while (key < TASK_END_ID) {
    if (persist_exists(key)) {
      key++;
    } else {
      return key - TASK_START_ID;  
    }
  }
  return key - TASK_START_ID;
}

static void task_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  char readTitle[32];
  persist_read_string(cell_index->row + TASK_START_ID, readTitle, sizeof(readTitle));
  menu_cell_title_draw(ctx, cell_layer, readTitle); 
}

static void task_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  ActionMenuConfig config = (ActionMenuConfig) {
    .root_level = rootMenuLevel,
    .colors = {
      .background = GColorWhite,
      .foreground = GColorBlack,
    },
    .align = ActionMenuAlignTop
  };
  char title[32];
  persist_read_string(cell_index->row + TASK_START_ID, title, sizeof(title));
  
  char *titleCopy = malloc(sizeof(title));  
  strcpy(titleCopy, title);
  selectedTaskItem = (Task){
    .id = cell_index->row + TASK_START_ID,
    .title = titleCopy
  };
  
  taskSelectionActionMenu = action_menu_open(&config);
}

static void createTasksMenu() {
  Layer *window_layer = window_get_root_layer(window);
  int16_t width = layer_get_frame(window_layer).size.w;
  int16_t height = layer_get_frame(window_layer).size.h; 
  GRect bounds = GRect(0, STATUS_BAR_LAYER_HEIGHT, width, height);

  // this is a workaround for the bug, that createMenu is called multiple times which causes for a lot of performance troubles.
  // the corret way is: create two menu layer, one for tasks, one for measurements, and set them to hidden when necessary
  if (taskMenuLayer != NULL) {
    menu_layer_destroy(taskMenuLayer);
  }
  
  taskMenuLayer = menu_layer_create(bounds);
  menu_layer_set_callbacks(taskMenuLayer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = task_menu_get_num_rows_callback,
    .draw_row = task_menu_draw_row_callback,
    .select_click = task_menu_select_callback,
  });
  
  /* bind the menu layer click provider to the window */
  menu_layer_set_click_config_onto_window(taskMenuLayer, window);
  
  layer_add_child(window_layer, menu_layer_get_layer(taskMenuLayer));
}

//------------------------------- MEASUREMENT MENU ----------------------------------------------------------------

static uint16_t measurement_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  uint32_t key = MEASUREMENTS_START_ID;
  while (key < MEASUREMENTS_END_ID) {
    if (persist_exists(key)) {
      key++;
    } else {
      return key - MEASUREMENTS_START_ID;
    }
  }
  return key - MEASUREMENTS_START_ID;
}

static void measurement_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  Measurement measurement;
  persist_read_data(cell_index->row + MEASUREMENTS_START_ID, &measurement, sizeof(measurement));
  
  char from[6];
  time_t startDatetime = (time_t) measurement.datetime;
  struct tm *start = localtime(&startDatetime);
  strftime(from, sizeof(from), "%H:%M", start);
  
  char * duration = malloc(20);
  strcpy(duration, from);
  
  if (measurement.endDatetime != 0) {
    char to[6];
    time_t endDatetime = (time_t) measurement.endDatetime;
    struct tm *end = localtime(&endDatetime);
    strftime(to, sizeof(to), "%H:%M", end);
  
    strcat(duration, " - ");
    strcat(duration, to);
  }
  
  /* read the title from the task by its id, because storing char* seems to be buggy. */
  char* taskTitle = malloc(50);
  if(persist_exists(measurement.taskId)) {
      char temp[32];
      persist_read_string(measurement.taskId, temp, sizeof(temp));
      strcpy(taskTitle, temp);
  } else {
    strcpy(taskTitle, "not found");
  }
  
  menu_cell_basic_draw(ctx, cell_layer, taskTitle, duration, NULL);
}

static void measurement_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  ActionMenuConfig config = (ActionMenuConfig) {
    .root_level = rootMenuLevel,
    .colors = {
      .background = GColorWhite,
      .foreground = GColorBlack,
    },
    .align = ActionMenuAlignTop
  };
  
  taskSelectionActionMenu = action_menu_open(&config);
}

static void createMeasurementsMenu() {
  Layer *window_layer = window_get_root_layer(window);
  int16_t width = layer_get_frame(window_layer).size.w;
  int16_t height = layer_get_frame(window_layer).size.h; 
  GRect bounds = GRect(0, STATUS_BAR_LAYER_HEIGHT, width, height);

  // this is a workaround for the bug, that createMenu is called multiple times which causes for a lot of performance troubles.
  // the corret way is: create two menu layer, one for tasks, one for measurements, and set them to hidden when necessary
  if (taskMenuLayer != NULL) {
    menu_layer_destroy(taskMenuLayer);
  }
  
  taskMenuLayer = menu_layer_create(bounds);
  menu_layer_set_callbacks(taskMenuLayer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = measurement_menu_get_num_rows_callback,
    .draw_row = measurement_menu_draw_row_callback,
    .select_click = measurement_menu_select_callback,
  });
  
  /* bind the menu layer click provider to the window */
  menu_layer_set_click_config_onto_window(taskMenuLayer, window);

  layer_add_child(window_layer, menu_layer_get_layer(taskMenuLayer));
}

//--------------------------------------------------------------------------------------------------------

static void storeMeasurement(Task selectedTaskItem, time_t* selectionTime) {
  uint32_t key = MEASUREMENTS_START_ID;
  while (key < MEASUREMENTS_END_ID) {
    if (persist_exists(key)) {
      key++;
    } else {
      /* update the end date of the last measurement */
      endTracking();
      
      char* copyOfTitle = malloc(50);
      strcpy(copyOfTitle, selectedTaskItem.title);
      
      Measurement data = (Measurement) {
        .taskId = selectedTaskItem.id,
        .datetime = *selectionTime
      };
      
      persist_write_data(key, &data, sizeof(data));  
      persist_write_int(LAST_MEASUREMENT_ID_ID, key);
      break;
    }
  }
}


/** Add a task to the task Items. */
static void addTask(uint32_t index, char *title) {
  char * copyOfTitle = malloc(strlen(title));
  strcpy(copyOfTitle, title);
  
  /* write new tasks into the storage. */
  persist_write_string(index, copyOfTitle);
}

static void clearTasks() {
  for(uint32_t key = TASK_START_ID; key < TASK_END_ID; key++) {
    if (persist_exists(key)) {
      persist_delete(key);
    }
  }
}

/**
 * Clear all stored measurments.
 */
static void clearMeasurements() {
  for(uint32_t key = MEASUREMENTS_START_ID; key < MEASUREMENTS_END_ID; key++) {
    if (persist_exists(key)) {
      persist_delete(key);
    }
  }
}

static void initTasksMenu() {
  rootMenuLevel = action_menu_level_create(4);
  action_menu_level_add_action(rootMenuLevel, "Start Tracking", action_performed_callback, (void*) StartTracking);
  action_menu_level_add_action(rootMenuLevel, "Interruption", action_performed_callback, (void*) Interruption);
  action_menu_level_add_action(rootMenuLevel, "End Tracking", action_performed_callback, (void*) EndTracking);
  action_menu_level_add_action(rootMenuLevel, "Show Measurements", action_performed_callback, (void*) ShowMeasurements);
}

static void initMeasurementsMenu() {
  rootMenuLevel = action_menu_level_create(2);
  action_menu_level_add_action(rootMenuLevel, "Show Tasks", action_performed_callback, (void*) ShowTasks);
  action_menu_level_add_action(rootMenuLevel, "Sync measurements", action_performed_callback, (void*) SyncMeasurements);
}

/**
 * Receiving all tasks from the android application.
 * First clears all existing tasks, then going throw all receiving tuples and adding the new tasks,
 * then creating the menu.
 */
static void receiveTasks(DictionaryIterator *iterator, void *context) { 
  clearTasks();
  
  Tuple *firstItem = dict_read_first(iterator);

  uint32_t index = TASK_START_ID;
  
  if (firstItem) {
    addTask(index, firstItem->value->cstring);
    index++;
    
    while(1) {
      Tuple *nextElement = dict_read_next(iterator);
      
      if (!nextElement) {
        break;
      }

      addTask(index, nextElement->value->cstring);
      index++;
    }
  }
  
  initTasksMenu();
  createTasksMenu();  
}

static void showText(const char* text) {  
  Layer *window_layer = window_get_root_layer(window);
  int16_t width = layer_get_frame(window_layer).size.w;
  GRect bounds = GRect(0, 0, width - 50, STATUS_BAR_LAYER_HEIGHT);
  
  if (selectedTaskLayer == NULL) {
    selectedTaskLayer = text_layer_create(bounds);
  }
  
  text_layer_set_text(selectedTaskLayer, text);
  layer_add_child(window_layer, text_layer_get_layer(selectedTaskLayer));
}

/**
 * The tick handler is updated every minute and displays the current date time of the current task.
 */
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  Layer *window_layer = window_get_root_layer(window);
    
  if (selectedTimeLayer == NULL) {
    int16_t width = layer_get_frame(window_layer).size.w;
    GRect bounds = GRect(80, 0, width, STATUS_BAR_LAYER_HEIGHT);
  
    selectedTimeLayer = text_layer_create(bounds);
  }

  if (selected) {  
    int diff = difftime(time(NULL), selectionTime);
      
    char diffChar[20];
    /* %02d means to show the first 2 digits of the double value and start with a 0 if it is below 9. */
    snprintf(diffChar, sizeof(diffChar), "%02d:%02d", diff/3600, (diff/60) % 60);
    
    char *copyDiff = malloc(sizeof(diffChar));
    strcpy(copyDiff, diffChar);
    
    text_layer_set_text(selectedTimeLayer, copyDiff);
  } else {
    text_layer_set_text(selectedTimeLayer, "");
  }
  
  layer_add_child(window_layer, text_layer_get_layer(selectedTimeLayer));
}

/**
 * Display the current task title.
 */
static void showSelectedTask() {
  showText(selectedTaskItem.title);
}

/** 
 * Sends all persistent measurements to the android application.
 * The id ranges are defined between MEASUREMENTS_START_ID and MEASUREMENTS_END_ID.
 * The format in which measurements are sent is not in json, but is a concatenated String in the following format:
 * The initialization String is "Sync;" and for each measurement it concatenates the title, startdatetime and enddatetime 
 * in the following scheme: "Title # Startdatetime # Enddatetime". Measurements are seperated with a comma (,).
 */
static void sendMeasurements() {
  DictionaryIterator *iter;
  AppMessageResult openResult = app_message_outbox_begin(&iter);

  if (openResult == APP_MSG_OK) {
    char* send;
    
    uint32_t lastKey = MEASUREMENTS_START_ID;
    while (lastKey < MEASUREMENTS_END_ID) {
      if (persist_exists(lastKey)) {
        lastKey++;
      } else {
        break;
      }
    }
      
    Measurement measurements[lastKey];
    for(uint32_t i = MEASUREMENTS_START_ID; i < lastKey; i++) {
      Measurement measurement;
      persist_read_data(i, &measurement, sizeof(measurement));
      
      measurements[i] = measurement;
    }
      
    send = malloc(1000);
    strcpy(send, "Sync;");
    for (uint32_t i = MEASUREMENTS_START_ID; i < lastKey; i++) {
      
      char* title = malloc(50);
      if(persist_exists(measurements[i].taskId)) {
        char temp[50];
        persist_read_string(measurements[i].taskId, temp, sizeof(temp));
        
        strcpy(title, temp);
      } else {
        strcpy(title, "Not found");
      }
      
      long int datetime = measurements[i].datetime;
      long int endDatetime = measurements[i].endDatetime;
      
      strcat(send, title);
      strcat(send, "#");
  
      char datetimeChar[15];
      snprintf(datetimeChar, sizeof(datetimeChar), "%li", datetime);
      
      char *copyDatetimeChar = malloc(sizeof(datetimeChar));
      strcpy(copyDatetimeChar, datetimeChar);
      strcat(send, copyDatetimeChar);
      
      strcat(send, "#");
      
      char endDatetimeChar[15];
      snprintf(endDatetimeChar, sizeof(endDatetimeChar), "%li", endDatetime);
      
      char *copyEndDatetimeChar = malloc(sizeof(endDatetimeChar));
      strcpy(copyEndDatetimeChar, endDatetimeChar);
      strcat(send, copyEndDatetimeChar);
      
      if (i != lastKey - 1) {
        strcat(send, ",");
      }
    }
    
    dict_write_cstring(iter, KEY_BUTTON, send);
    AppMessageResult result = app_message_outbox_send();
    if (result == APP_MSG_OK) {
      clearMeasurements();
    }
    
    free(send);
    showText("Sync success.");
  } else {
    showText("Sync failed.");
  }
}

static Task loadLastMeasurementTask() {
  if(persist_exists(LAST_MEASUREMENT_ID_ID)) {
    uint32_t lastMeasurementId = persist_read_int(LAST_MEASUREMENT_ID_ID);
    Measurement measurement;
    persist_read_data(lastMeasurementId, &measurement, sizeof(measurement));
    
    char* taskTitle = malloc(50);
    char temp[50];
    persist_read_string(measurement.taskId, temp, sizeof(temp));
    strcpy(taskTitle, temp);

    return (Task) {
      .id = measurement.taskId,
      .title = taskTitle
    };
  } else {
    return (Task) {
      .title = "No task found."
    };
  }  
}

static void setEndTimeForLastMeasurement(time_t endTime) {
  if (persist_exists(LAST_MEASUREMENT_ID_ID)) {
    uint32_t lastMeasurementId = persist_read_int(LAST_MEASUREMENT_ID_ID);
    Measurement measurement;
    persist_read_data(lastMeasurementId, &measurement, sizeof(measurement));
    
    measurement.endDatetime = endTime;
    
    persist_write_data(lastMeasurementId, &measurement, sizeof(measurement));
  }
}

/**
 * Update the last measurements end date time.
 */
static void endTracking() {
  time_t endTime;
  time(&endTime);
  setEndTimeForLastMeasurement(endTime);
}

static void handleInterruption() {
  time_t interruptionTime;
  time(&interruptionTime);
  setEndTimeForLastMeasurement(interruptionTime);
  
  selectedTaskItem = loadLastMeasurementTask();
  
  storeMeasurement(loadLastMeasurementTask(), &interruptionTime);
}

static void action_performed_callback(ActionMenu *actionMenu, const ActionMenuItem *action, void* context) {
  TaskSelectionAction selectionAction = (TaskSelectionAction) action_menu_item_get_action_data(action);
  
  switch (selectionAction) {
    case StartTracking:
      showSelectedTask();
      time(&selectionTime);
      storeMeasurement(selectedTaskItem, &selectionTime);
      selected = true;
      break;
    case Interruption:
      handleInterruption();
      break;
    case EndTracking:
      showText("");
      endTracking();
      selected = false;
      break;
    case ShowTasks:
      initTasksMenu();
      //menu_layer_reload_data(taskMenuLayer);
      createTasksMenu();
      //layer_set_hidden(menu_layer_get_layer(taskMenuLayer), false);
      //layer_set_hidden(menu_layer_get_layer(measurementsMenuLayer), true);
      break;
    case ShowMeasurements:
      initMeasurementsMenu();
      //menu_layer_reload_data(taskMenuLayer);
      createMeasurementsMenu();
      //layer_set_hidden(menu_layer_get_layer(taskMenuLayer), true);
      //layer_set_hidden(menu_layer_get_layer(measurementsMenuLayer), false);
      break;
    case SyncMeasurements:
      sendMeasurements();
      break;
    case Clear:
      clearMeasurements();
      return;
      break;
    default:
      break;
  }
}

/**
 * The health handler should be used to show a different action menu when the user did stand up from its table.
 * For software developers this usually means: Some interruption of the recent task to ask about conceptions, take a coffee, ...
 */
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventMovementUpdate) {
    vibes_short_pulse();
  }
}

static void select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  handleInterruption();  
}

static void click_config_provider(void *context) {
  window_long_click_subscribe(BUTTON_ID_UP, 700, select_long_click_handler, NULL);
}

static void storePredefined() {
  
  if (persist_exists(0)) {
    return;
  }
  char *taskTitle = "Interne Organisation und Ideen";
  addTask(1, taskTitle);
  
  addTask(2, "Task 2");
  
  addTask(3, "Task 3");
}

void handle_init(void) {
  /* Register the health handler callback. */
  //health_service_events_subscribe(health_handler, NULL);
  
  /* Receive tasks from the android application and store them inside of the database */
  app_message_register_inbox_received(receiveTasks);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  
  window = window_create();
  //window_set_click_config_provider(window, click_config_provider);
  window_stack_push(window, true);
  
  //storePredefined();
  
  /* Init and create the action menu. */
  initTasksMenu();
  createTasksMenu();
  
  /* Register a tick handler to update the current working time on the current measurement.
   * The measurement with time is shown at the top of the pebble window. 
   */
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler); 
  
  if(persist_exists(LAST_MEASUREMENT_ID_ID)) {
    uint32_t lastMeasurementId = persist_read_int(LAST_MEASUREMENT_ID_ID);
    Measurement measurement;
    persist_read_data(lastMeasurementId, &measurement, sizeof(measurement));
    
    if (measurement.endDatetime == 0) {
      char* taskTitle = malloc(50);
      char temp[50];
      persist_read_string(measurement.taskId, temp, sizeof(temp));
      strcpy(taskTitle, temp);
      
      selectedTaskItem = (Task) {
        .id = measurement.taskId,
        .title = taskTitle
      };
      selectionTime = (time_t) measurement.datetime;
      
      showSelectedTask();
      selected = true;
    } else {
      showText("No active found.");
    }
  } else {
    showText("No last measurement.");
  }
}

void handle_deinit(void) {
  menu_layer_destroy(taskMenuLayer);
  text_layer_destroy(selectedTaskLayer);
  text_layer_destroy(selectedTimeLayer);
  window_destroy(window);
}

int main(void) {  
  handle_init();
  app_event_loop();
  handle_deinit();
}