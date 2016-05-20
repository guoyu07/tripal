<?php

/**
 * Implements hook_field_storage_info().
 */
function tripal_chado_field_storage_info() {
  return array(
    'field_chado_storage' => array(
      'label' => t('Chado'),
      'description' => t('Stores fields in the local Chado database.'),
      'settings' => array(),
      // The logo_url key is supported by Tripal. It's not a Drupal key. It's
      // used for adding a logo or picture for the data store to help make it
      // more easily recognized on the  field_ui_field_overview_form. Ideally
      // the URL should point to a relative path on the local Drupal site.
      'logo_url' => url(drupal_get_path('module', 'tripal') . '/theme/images/250px-ChadoLogo.png'),
    ),
  );
}

/**
 * Implements hook_field_storage_write().
 */
function tripal_chado_field_storage_write($entity_type, $entity, $op, $fields) {

  // Get the bundle and the term for this entity.
  $bundle = tripal_load_bundle_entity(array('name' => $entity->bundle));
  $term = entity_load('TripalTerm', array('id' => $entity->term_id));
  $term = reset($term);
  $cvterm = $term->details['cvterm'];

  // Get the base table, type field and record_id from the entity.
  $base_table = $entity->chado_table;
  $type_field = $entity->chado_column;
  $record     = $entity->chado_record;
  $record_id  = $entity->chado_record_id;
  $base_schema = chado_get_schema($base_table);
  $base_pkey = $base_schema['primary key'][0];

  // Convert the fields into a key/value list of fields and their values.
  $field_vals = tripal_chado_field_storage_write_merge_fields($fields, $entity_type, $entity);

  // Write the record for the base table.  First get the values for this table
  // and set the record_id (update) or the type_id (insert)
  $values = $field_vals[$base_table][0];
  if ($record_id) {
    $values[$base_pkey] = $record_id;
  }
  else {
    $values[$type_field] = $cvterm->cvterm_id;
  }
  $base_record_id = tripal_chado_field_storage_write_table($base_table, $values);

  // If this is an insert then add the chado_entity record.
  if ($op == FIELD_STORAGE_INSERT) {
    // Add a record to the chado_entity table so that the data for the
    // fields can be pulled from Chado when loaded the next time.
    $record = array(
      'entity_id' => $entity->id,
      'record_id' => $base_record_id,
      'data_table' => $base_table,
      'type_table' => $base_table,
      'field' => $type_field,
    );
    $success = drupal_write_record('chado_entity', $record);
    if (!$success) {
      drupal_set_message('Unable to insert new Chado entity.', 'error');
    }
  }

  // Now that we have handled the base table, we need to handle linking tables.
  foreach ($field_vals as $table_name => $details) {
    // Skip the base table as we've already dealt with it.
    if ($table_name == $base_table) {
      continue;
    }
    foreach ($details as $delta => $values) {
      $record_id = tripal_chado_field_storage_write_table($table_name, $values);
    }
  }
}

/**
 * Write (inserts/oupdates) a nested array of values for a table.
 *
 * The $values array is of the same format used by chado_insert_record() and
 * chado_update_record().  However, both of those methods will use any nested
 * arrays (i.e. representing foreign keys) to select an appropriate record ID
 * that can be substituted as the value.  Here, the nested arrays are
 * either inserted or updated as well, but the choice is determined if the
 * primary key value is present.  If present an update occurs, if not present
 * then an insert occurs.
 *
 * This function is recursive and nested arrays from the lowest point of the
 * "tree" are dealt with first.
 *
 * @param $table_name
 *   The name of the table on which the insertion/update is performed.
 * @param $values
 *   The values array for the insertion.
 * @throws Exception
 * @return
 *   The unique record ID.
 */
function tripal_chado_field_storage_write_table($table_name, $values) {
   $schema = chado_get_schema($table_name);
   $fkeys = $schema['foreign keys'];
   $pkey = $schema['primary key'][0];

   // Before inserting or updating this table, recruse if there are any
   // nested FK array values.
   foreach ($values as $column => $value) {
     // If this value is an array then it must be a FK... let's recurse.
     if (is_array($value)) {

       // Find the name of the FK table for this column.
       $fktable_name = '';
       foreach ($fkeys as $fktable => $details) {
         foreach ($details['columns'] as $fkey_lcolumn => $fkey_rcolumn) {
           if ($fkey_lcolumn == $column) {
             $fktable_name = $fktable;
           }
         }
       }

       // Recurse on this recod.
       $record_id = tripal_chado_field_storage_write_table($fktable_name, $values[$column]);
       $values[$column] = $record_id;
     }
   }

   // Fields with a cardinality greater than 1 will often submit an
   // empty form.  We want to remove these empty submissions.  We can detect
   // them if all of the fields are empty.
   $num_empty = 0;
   foreach ($values as $column => $value) {
     if (!$value) {
       $num_empty++;
     }
   }
   if ($num_empty == count(array_keys($values))) {
     return '';
   }

   // If the primary key column has a value then this will be an udpate,
   // otherwise it's an insert.
   if (!array_key_exists($pkey, $values) or !$values[$pkey]) {
     // Before inserting, we want to make sure the record does not
     // already exist.  Using the unique constraint check for a matching record.
     $options = array('is_duplicate' => TRUE);
     $is_duplicate = chado_select_record($table_name, array('*'), $values, $options);
     if($is_duplicate) {
       $record = chado_select_record($table_name, array('*'), $values);
       return $record[0]->$pkey;
     }

     // Insert the values array as a new record in the table.
     $record = chado_insert_record($table_name, $values);
     if ($record === FALSE) {
       throw new Exception('Could not insert Chado record into table: "' . $tablename . '".');
     }
     return $record[$pkey];
   }
   // We have an incoming record_id so this is an update.
   else {
     // TODO: what if the unique constraint matches another record?  That is
     // not being tested for here.
     $match[$pkey] = $values[$pkey];
     if (!chado_update_record($table_name, $match, $values)) {
       drupal_set_message("Could not update Chado record in table: $tablename.", 'error');
     }
     return $values[$pkey];
   }
}

/**
 * Implements hook_field_storage_load().
 *
 * Responsible for loading the fields from the Chado database and adding
 * their values to the entity.
 */
function tripal_chado_field_storage_load($entity_type, $entities, $age,
    $fields, $options) {

  $load_current = $age == FIELD_LOAD_CURRENT;
  global $language;
  $langcode = $language->language;

  foreach ($entities as $id => $entity) {

    // Get the base table and record id for the fields of this entity.
    $details = db_select('chado_entity', 'ce')
      ->fields('ce')
      ->condition('entity_id', $entity->id)
      ->execute()
      ->fetchObject();

    if (!$details) {
      // TODO: what to do if record is missing!
    }

    // Get some values needed for loading the values from Chado.
    $base_table = $details->data_table;
    $type_field = $details->field;
    $record_id = $details->record_id;

    // Get this table's schema.
    $schema = chado_get_schema($base_table);
    $pkey_field = $schema['primary key'][0];

    // Get the base record if one exists
    $columns = array('*');
    $match = array($pkey_field => $record_id);
    $record = chado_generate_var($base_table, $match);
    $entity->chado_record = $record;

    // For now, expand all 'text' fields.
    // TODO: we want to be a bit smarter and allow the user to configure this
    // for now we'll expand.
    foreach ($schema['fields'] as $field_name => $details) {
      if ($schema['fields'][$field_name]['type'] == 'text') {
        $record = chado_expand_var($record, 'field', $base_table . '.' . $field_name);
      }
    }

    // Iterate through the entity's fields so we can get the column names
    // that need to be selected from each of the tables represented.
    $tables = array();
    foreach ($fields as $field_id => $ids) {

      // By the time this hook runs, the relevant field definitions have been
      // populated and cached in FieldInfo, so calling field_info_field_by_id()
      // on each field individually is more efficient than loading all fields in
      // memory upfront with field_info_field_by_ids().
      $field = field_info_field_by_id($field_id);
      $field_name = $field['field_name'];
      $field_type = $field['type'];
      $field_module = $field['module'];

      // Skip fields that don't map to a Chado table (e.g. kvproperty_adder).
      if (!array_key_exists('settings', $field) or !array_key_exists('chado_table', $field['settings'])) {
        continue;
      }

      // Get the Chado table and column for this field.
      $field_table = $field['settings']['chado_table'];
      $field_column = $field['settings']['chado_column'];

      // There are only two types of fields: 1) fields that represent a single
      // column of the base table, or 2) fields that represent a linked record
      // in a many-to-one relationship with the base table.

      // Type 1: fields from base tables.
      if ($field_table == $base_table) {
        // Set an empty value by default, and if there is a record, then update.
        $entity->{$field_name}['und'][0]['value'] = '';
        if ($record and property_exists($record, $field_column)) {
          // If the field column is an object then it's a FK to another table.
          // and because $record object is created by the chado_generate_var()
          // function we must go one more level deeper to get the value
          if (is_object($record->$field_column)) {
            $entity->{$field_name}['und'][0][$field_table . '__' . $field_column] = $record->$field_column->$field_column;
          }
          else {
            // For non FK fields we'll make the field value be the same
            // as the column value.
            $entity->{$field_name}['und'][0]['value'] = $record->$field_column;
            $entity->{$field_name}['und'][0][$field_table . '__' . $field_column] = $record->$field_column;
          }
        }

        // Allow the creating module to alter the value if desired.  The
        // module should do this if the field has any other form elements
        // that need populationg besides the value which was set above.
        module_load_include('inc', $field_module, 'includes/fields/' . $field_type);
        if (preg_match('/^chado/', $field_type) and class_exists($field_type)) {
          $field_obj = new $field_type();
          $field_obj->load($field, $entity, array('record' => $record));
        }

        $load_function = $field_type . '_load';
        if (function_exists($load_function)) {
          $load_function($field, $entity, $base_table, $record);
        }
      }

      // Type 2: fields for linked records.  These fields will have any number
      // of form elements that might need populating so we'll offload the
      // loading of these fields to the field itself.
      if ($field_table != $base_table) {

        // Set an empty value by default, and let the hook function update it.
        $entity->{$field_name}['und'][0]['value'] = '';
        $load_function = $field_type . '_load';
        module_load_include('inc', $field_module, 'includes/fields/' . $field_type);

        if (preg_match('/^chado/', $field_type) and class_exists($field_type)) {
          $field_obj = new $field_type();
          if (method_exists($field_obj, 'load')) {
            $field_obj->load($field, $entity, array('record' => $record));
          }
        }

        if (function_exists($load_function)) {
          $load_function($field, $entity, $base_table, $record);
        }
      }

    } // end: foreach ($fields as $field_id => $ids) {
  } // end: foreach ($entities as $id => $entity) {
}
/**
 * Merges the values of all fields into a single array keyed by table name.
 */
function tripal_chado_field_storage_write_merge_fields($fields, $entity_type, $entity) {
  $new_fields = array();

  // Iterate through all of the fields and organize them into a
  // new fields array keyed by the table name
  foreach ($fields as $field_id => $ids) {

    // Get the field name and information about it.
    $field = field_info_field_by_id($field_id);
    $field_name = $field['field_name'];
    // Some fields (e.g. chado_linker_cvterm_adder) don't add data to
    // Chado so they don't have a table, but they are still attached to the
    // entity. Just skip these.
    if (!array_key_exists('chado_table', $field['settings'])) {
      continue;
    }
    $chado_table = $field['settings']['chado_table'];
    $chado_column = $field['settings']['chado_column'];

    // Iterate through the field's items. Fields with cardinality ($delta) > 1
    // are multi-valued.
    $items = field_get_items($entity_type, $entity, $field_name);
    foreach ($items as $delta => $item) {
      foreach ($item as $item_name => $value) {
        $matches = array();
        if (preg_match('/^(.*?)__(.*?)$/', $item_name, $matches)) {
          $table_name = $matches[1];
          $column_name = $matches[2];

          // Is this a nested FK field? If so break it down into a sub array.
          if (preg_match('/^(.*?)--(.*?)$/', $column_name, $matches)) {
            $parent_item_name = $matches[1];
            $sub_item_name = $matches[2];
            $sub_item = tripal_chado_field_storage_expand_field($sub_item_name, $value);
            if (count(array_keys($sub_item))) {
              // If we've already encountered this table and column then we've
              // already seen the numeric FK value or we've already added a
              // subcolumn. If the former we want to convert this to an array
              // so we can add the details.
              if (!array_key_exists($table_name, $new_fields) or
                  !array_key_exists($delta, $new_fields[$table_name]) or
                  !array_key_exists($parent_item_name, $new_fields[$table_name][$delta]) or
                  !is_array($new_fields[$table_name][$delta][$parent_item_name])) {
                $new_fields[$table_name][$delta][$parent_item_name] = array();
              }
              $new_fields[$table_name][$delta][$parent_item_name] += $sub_item;
            }
          }
          else {
            // If not seen this table and column then just add it. If we've
            // already seen it then it means it's a FK field and we've already
            // added subfields so do nothing.
            if (!array_key_exists($table_name, $new_fields) or
                !array_key_exists($delta, $new_fields[$table_name]) or
                !array_key_exists($column_name, $new_fields[$table_name][$delta])) {
              $new_fields[$table_name][$delta][$column_name] = $value;
            }
          }
        }
      }
      // If there is no value set for the field using the
      // [table_name]__[field name] naming schema then check if a 'value' item
      // is present and if so use that.
      if ((!array_key_exists($chado_table, $new_fields) or
           !array_key_exists($delta, $new_fields[$chado_table]) or
           !array_key_exists($chado_column, $new_fields[$chado_table][$delta])) and
          array_key_exists('value', $items[$delta]) and
          !is_array($items[$delta]['value'])) {
        $new_fields[$chado_table][$delta][$chado_column] = $items[$delta]['value'];
      }
    }
  }

  return $new_fields;
}

/**
 * Recurses through a field's item name breaking it into a nested array.
 *
 *
 */
function tripal_chado_field_storage_expand_field($item_name, $value) {

   $matches = array();
   if (preg_match('/^(.*?)--(.*?)$/', $item_name, $matches)) {
     $parent_item_name = $matches[1];
     $sub_item_name = $matches[2];
     $sub_item = tripal_chado_field_storage_expand_field($sub_item_name, $value);
     return array($parent_item_name => $sub_item);
   }
   else {
     return array($item_name => $value);
   }
}

/**
 * Implements hook_field_storage_query().
 *
 * Used by EntityFieldQuery to find the entities having certain field values.
 *
 * We do not support use of the EntityFieldQuery API for Tripal based fields
 * because EFQ doesn't support when multiple storage backends are used. Instead
 * use the TripalFieldQuery class and implement the hook_storage_tquery()
 * function.
 */
function tripal_chado_field_storage_query($query) {

}
/**
 * Implements hook_field_storage_tquery().
 *
 * Used by TripalFieldQuery to find the entities having certain field values.
 *
 * @param $conditions
 */
function tripal_chado_field_storage_tquery($conditions) {
  $filter = array();
  foreach ($conditions as $index => $condition) {
    $field = $condition['field'];
    $field_type = $field['type'];
    $field_module = $field['module'];
    $settings = $field['settings'];
    $chado_table = $settings['chado_table'];
    $chado_column = $settings['chado_column'];

    // Allow the creating module to alter the value if desired.
    $value = '';
    module_load_include('inc', $field_module, 'includes/fields/' . $field_type);
    if (preg_match('/^chado/', $field_type) and class_exists($field_type)) {
      $field_obj = new $field_type();
      if (method_exists($field_obj, 'query')) {
        $value = $field_obj->query($condition);
      }
    }
    // If there is no field to rewrite the value then use defaults.
    else {
      $value = $condition['value'];
    }

    // use the appropriate operator.
    $operator = $condition['operator'];
    switch ($operator) {
      case '=':
        $filter[$chado_table][$chado_column] = $condition['value'];
        break;
      case '>':
      case '>=':
      case '<':
      case '<=':
        $filter[$chado_table][$chado_column] = array(
          'op' => $operator,
          'data' => $value,
        );
        break;
      case '<>':
        $filter[$chado_table][$chado_column] = array(
          'op' => 'NOT',
          'data' => $value,
        );
        break;
      case 'CONTAINS':
        break;
        $filter[$chado_table][$chado_column] = array(
          'op' => 'LIKE',
          'data' => '%' . $value . '%',
        );
      case 'STARTS WITH':
        $filter[$chado_table][$chado_column] = array(
          'op' => 'LIKE',
          'data' => $value . '%',
        );
        break;
      default:
        // unrecognized operation.
        break;
    }
  }

  // Iterate through the filters and perform the query
  $entity_ids = array();
  foreach ($filter as $chado_table => $values) {
    // First get the matching record IDs from the Chado table.
    $schema = chado_get_schema($chado_table);
    $pkey = $schema['primary key'][0];
    $results = chado_select_record($chado_table, array($pkey), $values);

    $record_ids = array();
    foreach ($results as $result) {
      $record_ids[] = $result->$pkey;
    }

    // Next look for matching IDs in the chado_entity table.
    $filter_ids = array();
    $results = db_select('chado_entity', 'CE')
      ->fields('CE', array('entity_id'))
      ->condition('record_id', $record_ids)
      ->execute();
    while ($result = $results->fetchObject()) {
      $filter_ids[] = $result->entity_id;
    }

    // Take the intersection of IDs in this filter with those in the
    // final $entity_ids;
    if (count($entity_ids) == 0) {
      $entity_ids = $filter_ids;
    }
    else {
      $entity_ids = array_intersect($entity_ids, $filter_ids);
    }
  }

  return $entity_ids;
}