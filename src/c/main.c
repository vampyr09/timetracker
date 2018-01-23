#include <pebble.h>

#define PERSIST_DATA_MAX_LENGTH 256
#define PERSIST_STRING_MAX_LENGTH PERSIST_DATA_MAX_LENGTH 

#define TASK_START_ID 1 
#define TASK_END_ID 49
#define MEASUREMENTS_START_ID 50
#define MEASUREMENTS_END_ID 149
#define INTERRUPTION_START_ID 150
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
  SyncMeasurements,
  Clear
} TaskSelectionAction;

typedef struct {
  TaskSelectionAction action;
} Context;

typedef struct {
  char* title;
} Task;

typedef struct {
  char* title;
  long int datetime;
  long int endDatetime;
} Measurement;

#define KEY_BUTTON 0

Window *window;

TextLayer *selectedTaskLayer;
TextLayer *selectedTimeLayer;
time_t selectionTime;

/** The menu to display all possible tasks (are part of the listTaskLayer). Asked from the android configuration. */
MenuLayer *taskMenuLayer;

Task selectedTaskItem;

ActionMenu *taskSelectionActionMenu;
ActionMenuLevel *rootMenuLevel, *customMenuLevel;

bool selected = false;

//methods
static void showText(const char* text);
static void addTask(uint32_t index, char *title);
static void createMenu();
static void initActionMenu();
static void action_performed_callback(ActionMenu *actionMenu, const ActionMenuItem *action, void* context);
static void endTracking();

static uint32_t getLastUsedKey() {
  uint32_t key = 0;
  while (key < 255) {
    if (persist_exists(key)) {
      key++;
    } else {
      return key;
    }
  }
  
  return key;
}

static void storeMeasurement(Task selectedTaskItem, time_t* selectionTime) {
  uint32_t key = MEASUREMENTS_START_ID;
  while (key < MEASUREMENTS_END_ID) {
    if (persist_exists(key)) {
      key++;
    } else {
      /* update the end date of the last measurement */
      endTracking();
      
      Measurement data = (Measurement) {
        .title = selectedTaskItem.title,
        .datetime = *selectionTime
      };
      persist_write_data(key, &data, sizeof(data));  
      persist_write_int(LAST_MEASUREMENT_ID_ID, key);
      break;
    }
  }
  char measurements[] = "0000";
  snprintf(measurements, sizeof(measurements), "%i", (int) key);
    
  showText(measurements);
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return (uint16_t) getLastUsedKey();
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  char readTitle[32];
  persist_read_string(cell_index->row, readTitle, sizeof(readTitle));
  menu_cell_title_draw(ctx, cell_layer, readTitle); 
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  ActionMenuConfig config = (ActionMenuConfig) {
    .root_level = rootMenuLevel,
    .colors = {
      .background = GColorWhite,
      .foreground = GColorBlack,
    },
    .align = ActionMenuAlignTop
  };
  char title[32];
  persist_read_string(cell_index->row, title, sizeof(title));
  
  char *titleCopy = malloc(sizeof(title));  
  strcpy(titleCopy, title);
  selectedTaskItem = (Task){.title = titleCopy};
  
  taskSelectionActionMenu = action_menu_open(&config);
}

/** Create the Menu */
static void createMenu() {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Create menu!");  
  
  Layer *window_layer = window_get_root_layer(window);
  int16_t width = layer_get_frame(window_layer).size.w;
  int16_t height = layer_get_frame(window_layer).size.h; 
  GRect bounds = GRect(0, STATUS_BAR_LAYER_HEIGHT, width, height);

  taskMenuLayer = menu_layer_create(bounds);
  menu_layer_set_callbacks(taskMenuLayer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = menu_get_num_rows_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
  });
  
  /* bind the menu layer click provider to the window */
  menu_layer_set_click_config_onto_window(taskMenuLayer, window);

  layer_add_child(window_layer, menu_layer_get_layer(taskMenuLayer));
}

/** Add a task to the task Items. */
static void addTask(uint32_t index, char *title) {
  char * copyOfTitle = malloc(strlen(title));
  strcpy(copyOfTitle, title);
  
  /* write new tasks into the storage. */
  persist_write_string(index, copyOfTitle);
}

static void clearAll() {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Clear all!");
  
  /* clear the old tasks when receiving new from android. */
  for(uint32_t key = 0; key < 256; key++) {
    if (persist_exists(key)) {
      char buffer[] = "0000";
      snprintf(buffer, sizeof(buffer), "%d", (int) key);
    
      APP_LOG(APP_LOG_LEVEL_ERROR, buffer);
      persist_delete(key);
    }
  }
  createMenu();
}

static void clearMeasurements() {
  for(uint32_t key = MEASUREMENTS_START_ID; key < MEASUREMENTS_END_ID; key++) {
    if (persist_exists(key)) {
      persist_delete(key);
    }
  }
}

static void receiveTasks(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message received!");
   
  clearAll();
  
  Tuple *firstItem = dict_read_first(iterator);

  uint32_t index = 0;
  
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
  
  createMenu();
}

static void initActionMenu() {
  rootMenuLevel = action_menu_level_create(5);
  action_menu_level_add_action(rootMenuLevel, "Start Tracking", action_performed_callback, (void*) StartTracking);
  action_menu_level_add_action(rootMenuLevel, "Interruption", action_performed_callback, (void*) Interruption);
  action_menu_level_add_action(rootMenuLevel, "End Tracking", action_performed_callback, (void*) EndTracking);
  action_menu_level_add_action(rootMenuLevel, "Sync measurements", action_performed_callback, (void*) SyncMeasurements);
  action_menu_level_add_action(rootMenuLevel, "Clear", action_performed_callback, (void*) Clear);
}

static void showText(const char* text) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Show text: %s", text);
  
  Layer *window_layer = window_get_root_layer(window);
  int16_t width = layer_get_frame(window_layer).size.w;
  GRect bounds = GRect(0, 0, width - 50, STATUS_BAR_LAYER_HEIGHT);
  
  selectedTaskLayer = text_layer_create(bounds);
  
  //char *display = malloc(sizeof(text));
  //strcpy(display, text);
  
  text_layer_set_text(selectedTaskLayer, text);
  layer_add_child(window_layer, text_layer_get_layer(selectedTaskLayer));
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (selected) {  
    Layer *window_layer = window_get_root_layer(window);
    int16_t width = layer_get_frame(window_layer).size.w;
    GRect bounds = GRect(80, 0, width, STATUS_BAR_LAYER_HEIGHT);
    selectedTimeLayer = text_layer_create(bounds);
  
    int diff = difftime(time(NULL), selectionTime);
      
    char diffChar[20];
    snprintf(diffChar, sizeof(diffChar), "%02d:%02d", diff/3600, (diff/60) % 60);
    
    char *copyDiff = malloc(sizeof(diffChar));
    strcpy(copyDiff, diffChar);
    
    text_layer_set_text(selectedTimeLayer, copyDiff);
    layer_add_child(window_layer, text_layer_get_layer(selectedTimeLayer));
  }
}

static void showSelectedTask() {
  showText(selectedTaskItem.title);
}

static void sendMeasurements() {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

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
    char* title = measurements[i].title;
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
    
    free(copyDatetimeChar);
    free(copyEndDatetimeChar);
    free(title);
    
    if (i != lastKey - 1) {
      strcat(send, ",");
    }
  }
  
  dict_write_cstring(iter, KEY_BUTTON, send);
  app_message_outbox_send();
  free(send);
  //free measurements!
}

static void endTracking() {
  if (persist_exists(LAST_MEASUREMENT_ID_ID)) {
    uint32_t lastMeasurementId = persist_read_int(LAST_MEASUREMENT_ID_ID);
    Measurement measurement;
    persist_read_data(lastMeasurementId, &measurement, sizeof(measurement));
    
    time_t endTime;
    time(&endTime);
    measurement.endDatetime = endTime;
    
    persist_write_data(lastMeasurementId, &measurement, sizeof(measurement));
  }
}

static void action_performed_callback(ActionMenu *actionMenu, const ActionMenuItem *action, void* context) {
  TaskSelectionAction selectionAction = (TaskSelectionAction) action_menu_item_get_action_data(action);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Selected task item action menu: %s.", selectedTaskItem.title);  
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App outbox message began.");
  
  switch (selectionAction) {
    case StartTracking:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Start tracking triggered for: %s", selectedTaskItem.title);
      //showSelectedTask();
      time(&selectionTime);
      storeMeasurement(selectedTaskItem, &selectionTime);
      selected = true;
      break;
    case EndTracking:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "End tracking triggered for: %s!", selectedTaskItem.title);
      showText("");
      endTracking();
      selected = false;
      break;
    case SyncMeasurements:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync measurmeets triggered!");
      sendMeasurements();
      break;
    case Clear:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Clear all triggered!");
      clearMeasurements();
      app_message_outbox_send();
      return;
      break;
    default:
      break;
  }
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Action performed.");
}

static void storePredefined() {
  
  if (persist_exists(0)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Predefined tasks already inside of the database!");
    return;
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "Initializing predefined task!");
  
  char *taskTitle = "Task 1";
  addTask(0, taskTitle);
  
  addTask(1, "Task 2");
  
  addTask(2, "Task 3");
}

void handle_init(void) {
  app_message_register_inbox_received(receiveTasks);

  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  
  //storePredefined();
  window = window_create();
  window_stack_push(window, true);

  initActionMenu();  
  createMenu();
  
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
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