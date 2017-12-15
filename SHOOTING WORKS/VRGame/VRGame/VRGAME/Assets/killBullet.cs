using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class killBullet : MonoBehaviour {

	// Use this for initialization
	void Start () {
        StartCoroutine("KillBullet");
	}
	
	// Update is called once per frame
	void Update () {
		
	}
    IEnumerator KillBullet()
    {
        yield return new WaitForSeconds(10f);
        Destroy(gameObject);
    }

    private void OnTriggerEnter(Collider other)
    {
        Destroy(gameObject);
       
    }
}
